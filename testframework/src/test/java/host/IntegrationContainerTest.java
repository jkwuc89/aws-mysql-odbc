/*
 * AWS ODBC Driver for MySQL
 * Copyright Amazon.com Inc. or affiliates.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

package host;

import com.amazonaws.util.StringUtils;

import static org.junit.jupiter.api.Assertions.fail;

import java.io.IOException;
import java.net.UnknownHostException;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;
import org.testcontainers.containers.GenericContainer;
import org.testcontainers.containers.Network;
import org.testcontainers.containers.ToxiproxyContainer;
import utility.AuroraClusterInfo;
import utility.AuroraTestUtility;
import utility.ContainerHelper;

public class IntegrationContainerTest {
  private static final int MYSQL_PORT = 3306;
  private static final String TEST_CONTAINER_NAME = "test-container";
  private static final String TEST_DATABASE = "test";
  private static final String TEST_DSN = System.getenv("TEST_DSN");
  private static final String TEST_USERNAME =
      !StringUtils.isNullOrEmpty(System.getenv("TEST_USERNAME")) ?
          System.getenv("TEST_USERNAME") : "my_test_username";
  private static final String TEST_PASSWORD =
      !StringUtils.isNullOrEmpty(System.getenv("TEST_PASSWORD")) ?
          System.getenv("TEST_PASSWORD") : "my_test_password";
  private static final String ACCESS_KEY = System.getenv("AWS_ACCESS_KEY_ID");
  private static final String SECRET_ACCESS_KEY = System.getenv("AWS_SECRET_ACCESS_KEY");
  private static final String SESSION_TOKEN = System.getenv("AWS_SESSION_TOKEN");
  private static final String DOCKER_UID = "1001";
  private static final String COMMUNITY_SERVER = "mysql-instance";

  private static final String DRIVER_LOCATION = System.getenv("DRIVER_PATH");
  private static final String TEST_DB_CLUSTER_IDENTIFIER = System.getenv("TEST_DB_CLUSTER_IDENTIFIER");
  private static final String PROXIED_DOMAIN_NAME_SUFFIX = ".proxied";
  private static List<ToxiproxyContainer> proxyContainers = new ArrayList<>();

  private static final ContainerHelper containerHelper = new ContainerHelper();
  private static final AuroraTestUtility auroraUtil = new AuroraTestUtility("us-east-2");

  private static int mySQLProxyPort;
  private static List<String> mySqlInstances = new ArrayList<>();
  private static GenericContainer<?> testContainer;
  private static GenericContainer<?> mysqlContainer;
  private static String dbHostCluster = "";
  private static String dbHostClusterRo = "";
  private static String runnerIP = null;
  private static String dbConnStrSuffix = "";

  private static final Network NETWORK = Network.newNetwork();

  @BeforeAll
  static void setUp() throws InterruptedException, UnknownHostException {
    testContainer = createTestContainer(NETWORK);
  }

  @AfterAll
  static void tearDown() {
    if (!StringUtils.isNullOrEmpty(ACCESS_KEY) && !StringUtils.isNullOrEmpty(SECRET_ACCESS_KEY) && !StringUtils.isNullOrEmpty(dbHostCluster)) {
      if (StringUtils.isNullOrEmpty(TEST_DB_CLUSTER_IDENTIFIER)) {
        auroraUtil.deleteCluster();
      } else {
        auroraUtil.deleteCluster(TEST_DB_CLUSTER_IDENTIFIER);
      }

      auroraUtil.ec2DeauthorizesIP(runnerIP);

      for (ToxiproxyContainer proxy : proxyContainers) {
        proxy.stop();
      }
    }

    testContainer.stop();
    if (mysqlContainer != null) {
      mysqlContainer.stop();
    }
  }

  @Test
  public void testRunCommunityTestInContainer()
      throws UnsupportedOperationException, IOException, InterruptedException {
    setupCommunityTests(NETWORK);

    try {
      // Allow the non root user to access this folder which contains log files. Required to run tests
      testContainer.execInContainer("chown", DOCKER_UID, "/app/test/Testing/Temporary");
    } catch (Exception e) {
      fail("Test container was not initialized correctly");
    }

    containerHelper.runCTest(testContainer, "test");
  }

  @Test
  public void testRunFailoverTestInContainer()
      throws UnsupportedOperationException, IOException, InterruptedException {
    setupFailoverIntegrationTests(NETWORK);
    
    containerHelper.runExecutable(testContainer, "integration/bin", "integration");
  }

  protected static GenericContainer<?> createTestContainer(final Network network) {
    return containerHelper.createTestContainer(
            "odbc/rds-test-container",
            DRIVER_LOCATION)
        .withNetworkAliases(TEST_CONTAINER_NAME)
        .withNetwork(network)
        .withEnv("TEST_DSN", TEST_DSN)
        .withEnv("TEST_UID", TEST_USERNAME)
        .withEnv("TEST_PASSWORD", TEST_PASSWORD)
        .withEnv("TEST_DATABASE", TEST_DATABASE)
        .withEnv("MYSQL_PORT", Integer.toString(MYSQL_PORT))
        .withEnv("ODBCINI", "/etc/odbc.ini")
        .withEnv("ODBCINST", "/etc/odbcinst.ini")
        .withEnv("ODBCSYSINI", "/etc")
        .withEnv("TEST_DRIVER", "/app/lib/libmyodbc8a.so");
  }

  private void setupFailoverIntegrationTests(final Network network) throws InterruptedException, UnknownHostException {
    if (!StringUtils.isNullOrEmpty(ACCESS_KEY) && !StringUtils.isNullOrEmpty(SECRET_ACCESS_KEY)) {
      // Comment out below to not create a new cluster & instances
      AuroraClusterInfo clusterInfo = auroraUtil.createCluster(TEST_USERNAME, TEST_PASSWORD, TEST_DB_CLUSTER_IDENTIFIER, TEST_DATABASE);

      // Comment out getting public IP to not add & remove from EC2 whitelist
      runnerIP = auroraUtil.getPublicIPAddress();
      auroraUtil.ec2AuthorizeIP(runnerIP);

      dbConnStrSuffix = clusterInfo.getClusterSuffix();
      dbHostCluster = clusterInfo.getClusterEndpoint();
      dbHostClusterRo = clusterInfo.getClusterROEndpoint();

      mySqlInstances = clusterInfo.getInstances();

      proxyContainers = containerHelper.createProxyContainers(network, mySqlInstances, PROXIED_DOMAIN_NAME_SUFFIX);
      for (ToxiproxyContainer container : proxyContainers) {
        container.start();
      }
      mySQLProxyPort = containerHelper.createAuroraInstanceProxies(mySqlInstances, proxyContainers, MYSQL_PORT);

      proxyContainers.add(containerHelper.createAndStartProxyContainer(
          network,
          "toxiproxy-instance-cluster",
          dbHostCluster + PROXIED_DOMAIN_NAME_SUFFIX,
          dbHostCluster,
          MYSQL_PORT,
          mySQLProxyPort)
      );

      proxyContainers.add(containerHelper.createAndStartProxyContainer(
          network,
          "toxiproxy-ro-instance-cluster",
          dbHostClusterRo + PROXIED_DOMAIN_NAME_SUFFIX,
          dbHostClusterRo,
          MYSQL_PORT,
          mySQLProxyPort)
      );
    }

    testContainer
      .withEnv("AWS_ACCESS_KEY_ID", ACCESS_KEY)
      .withEnv("AWS_SECRET_ACCESS_KEY", SECRET_ACCESS_KEY)
      .withEnv("AWS_SESSION_TOKEN", SESSION_TOKEN)
      .withEnv("TOXIPROXY_CLUSTER_NETWORK_ALIAS", "toxiproxy-instance-cluster")
      .withEnv("TOXIPROXY_RO_CLUSTER_NETWORK_ALIAS", "toxiproxy-ro-instance-cluster")
      .withEnv("PROXIED_DOMAIN_NAME_SUFFIX", PROXIED_DOMAIN_NAME_SUFFIX)
      .withEnv("TEST_SERVER", dbHostCluster)
      .withEnv("TEST_RO_SERVER", dbHostClusterRo)
      .withEnv("DB_CONN_STR_SUFFIX", "." + dbConnStrSuffix)
      .withEnv("PROXIED_CLUSTER_TEMPLATE", "?." + dbConnStrSuffix + PROXIED_DOMAIN_NAME_SUFFIX);
        
    // Add mysql instances & proxies to container env
    for (int i = 0; i < mySqlInstances.size(); i++) {
      // Add instance
      testContainer.addEnv(
          "MYSQL_INSTANCE_" + (i + 1) + "_URL",
          mySqlInstances.get(i));

      // Add proxies
      testContainer.addEnv(
          "TOXIPROXY_INSTANCE_" + (i + 1) + "_NETWORK_ALIAS",
          "toxiproxy-instance-" + (i + 1));
    }
    testContainer.addEnv("MYSQL_PROXY_PORT", Integer.toString(mySQLProxyPort));
    testContainer.start();

    System.out.println("Toxyproxy Instances port: " + mySQLProxyPort);
  }

  private void setupCommunityTests(final Network network) {
    mysqlContainer = ContainerHelper.createMysqlContainer(network);
    mysqlContainer.start();

    testContainer
      .withEnv("TEST_SERVER", COMMUNITY_SERVER)
      .withCreateContainerCmdModifier(cmd -> cmd.withUser(DOCKER_UID + ":" + DOCKER_UID));
    testContainer.start();
  }
}
