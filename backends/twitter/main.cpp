#include "transport/config.h"
#include "transport/networkplugin.h"
#include "transport/logging.h"
#include "Swiften/Swiften.h"
#include "unistd.h"
#include "signal.h"
#include "sys/wait.h"
#include "sys/signal.h"
#include <boost/algorithm/string.hpp>
#include "twitcurl.h"

#include <iostream>
#include <map>
#include <vector>
#include <cstdio>
#include "userdb.h"

using namespace boost::filesystem;
using namespace boost::program_options;
using namespace Transport;

DEFINE_LOGGER(logger, "Twitter Backend");
Swift::SimpleEventLoop *loop_; // Event Loop
class TwitterPlugin; // The plugin
TwitterPlugin * np = NULL;

class TwitterPlugin : public NetworkPlugin {
	public:
		Swift::BoostNetworkFactories *m_factories;
		Swift::BoostIOServiceThread m_boostIOServiceThread;
		boost::shared_ptr<Swift::Connection> m_conn;

		TwitterPlugin(Config *config, Swift::SimpleEventLoop *loop, const std::string &host, int port) : NetworkPlugin() {
			this->config = config;
			m_factories = new Swift::BoostNetworkFactories(loop);
			m_conn = m_factories->getConnectionFactory()->createConnection();
			m_conn->onDataRead.connect(boost::bind(&TwitterPlugin::_handleDataRead, this, _1));
			m_conn->connect(Swift::HostAddressPort(Swift::HostAddress(host), port));
			
			db = new UserDB(std::string("user.db"));
			registeredUsers = db->getRegisteredUsers();
			
			LOG4CXX_INFO(logger, "Starting the plugin.");
		}

		~TwitterPlugin() {
			delete db;
			std::map<std::string, twitCurl*>::iterator it;
			for(it = sessions.begin() ; it != sessions.end() ; it++) delete it->second;
		}

		// Send data to NetworkPlugin server
		void sendData(const std::string &string) {
			m_conn->write(Swift::createSafeByteArray(string));
		}

		// Receive date from the NetworkPlugin server and invoke the appropirate payload handler (implement in the NetworkPlugin class)
		void _handleDataRead(boost::shared_ptr<Swift::SafeByteArray> data) {
			std::string d(data->begin(), data->end());
			handleDataRead(d);
		}
		
		// User trying to login into his twitter account
		void handleLoginRequest(const std::string &user, const std::string &legacyName, const std::string &password) {
			if(connectionState.count(user) && (connectionState[user] == NEW || 
						                        connectionState[user] == CONNECTED || 
												connectionState[user] == WAITING_FOR_PIN)) {
				LOG4CXX_INFO(logger, std::string("A session corresponding to ") + user + std::string(" is already active"))
				return;
			}
			
			LOG4CXX_INFO(logger, std::string("Received login request for ") + user)
			//twitCurl &twitterObj = sessions[user];
			//std::string myOAuthAccessTokenSecret, myOAuthAccessTokenKey;
        	//twitterObj.getOAuth().getOAuthTokenKey( myOAuthAccessTokenKey );
        	//twitterObj.getOAuth().getOAuthTokenSecret( myOAuthAccessTokenSecret );

			//if(myOAuthAccessTokenSecret.size() && myOAuthAccessTokenKey.size()) {	
			//}
			
			std::string username = user.substr(0,user.find('@'));
			std::string passwd = password;
			LOG4CXX_INFO(logger, username + "  " + passwd)

			sessions[user] = new twitCurl();
			handleConnected(user);
			handleBuddyChanged(user, "twitter-account", "twitter", std::vector<std::string>(), pbnetwork::STATUS_ONLINE);
	        
//			std::string ip = "10.93.0.36";
//			std::string port = "3128";
//			std::string puser = "cs09s022";
//			std::string ppasswd = "";
//			sessions[user]->setProxyServerIp(ip);
//	        sessions[user]->setProxyServerPort(port);
//	        sessions[user]->setProxyUserName(puser);
//	        sessions[user]->setProxyPassword(ppasswd);
			
			connectionState[user] = NEW;
			
			sessions[user]->setTwitterUsername(username);
			sessions[user]->setTwitterPassword(passwd); 
			sessions[user]->getOAuth().setConsumerKey( std::string( "qxfSCX7WN7SZl7dshqGZA" ) );
			sessions[user]->getOAuth().setConsumerSecret( std::string( "ypWapSj87lswvnksZ46hMAoAZvST4ePGPxAQw6S2o" ) );
			
			if(registeredUsers.count(user) == 0) {	
				std::string authUrl;
				if (sessions[user]->oAuthRequestToken( authUrl ) == false ) {
					LOG4CXX_ERROR(logger, "Error creating twitter authorization url!");
					handleLogoutRequest(user, username);
					return;
				}
				handleMessage(user, "twitter-account", std::string("Please visit the following link and authorize this application: ") + authUrl);
				handleMessage(user, "twitter-account", std::string("Please reply with the PIN provided by twitter. Prefix the pin with 'pin:'. Ex. 'pin: 1234'"));
				connectionState[user] = WAITING_FOR_PIN;	
			} else {
				std::vector<std::string> keysecret;
				db->fetch(user, keysecret);
				sessions[user]->getOAuth().setOAuthTokenKey( keysecret[0] );
				sessions[user]->getOAuth().setOAuthTokenSecret( keysecret[1] );
				connectionState[user] = CONNECTED;
			}
		}
		
		// User logging out
		void handleLogoutRequest(const std::string &user, const std::string &legacyName) {
			delete sessions[user];
			sessions[user] = NULL;
			connectionState[user] = DISCONNECTED;
		}


		void handleMessageSendRequest(const std::string &user, const std::string &legacyName, const std::string &message, const std::string &xhtml = "") {
			LOG4CXX_INFO(logger, "Sending message from " << user << " to " << legacyName << ".");
			if(legacyName == "twitter-account") {
				std::string cmd = message.substr(0, message.find(':'));
				std::string data = message.substr(message.find(':') + 1);
				
				handleMessage(user, "twitter-account", cmd + " " + data);

				if(cmd == "pin") {
					sessions[user]->getOAuth().setOAuthPin( data );
					sessions[user]->oAuthAccessToken();
					
					std::string OAuthAccessTokenKey, OAuthAccessTokenSecret;
					sessions[user]->getOAuth().getOAuthTokenKey( OAuthAccessTokenKey );
					sessions[user]->getOAuth().getOAuthTokenSecret( OAuthAccessTokenSecret );
					db->insert(UserData(user, OAuthAccessTokenKey, OAuthAccessTokenSecret));
					registeredUsers.insert(user);

					connectionState[user] = CONNECTED;
					LOG4CXX_INFO(logger, "Sent PIN " << data << " and obtained access token");
				}

				if(cmd == "status") {
					if(connectionState[user] != CONNECTED) {
						LOG4CXX_ERROR(logger, "Trying to update status for " << user << " when not connected!");
						return;
					}

					LOG4CXX_INFO(logger, "Updating status for " << user << ": " << data);
					std::string replyMsg; 
					if( sessions[user]->statusUpdate( data ) ) {
						sessions[user]->getLastWebResponse( replyMsg );
						LOG4CXX_INFO(logger, "twitterClient:: twitCurl::statusUpdate web response: " << replyMsg );
					}
					else {
						sessions[user]->getLastCurlError( replyMsg );
						LOG4CXX_INFO(logger, "twitterClient:: twitCurl::statusUpdate error: " << replyMsg );
					}
				}
			}
		}

		void handleBuddyUpdatedRequest(const std::string &user, const std::string &buddyName, const std::string &alias, const std::vector<std::string> &groups) {
			LOG4CXX_INFO(logger, user << ": Added buddy " << buddyName << ".");
			handleBuddyChanged(user, buddyName, alias, groups, pbnetwork::STATUS_ONLINE);
		}

		void handleBuddyRemovedRequest(const std::string &user, const std::string &buddyName, const std::vector<std::string> &groups) {

		}

	private:
		enum status {NEW, WAITING_FOR_PIN, CONNECTED, DISCONNECTED};
		Config *config;
		UserDB *db;
		std::set<std::string> registeredUsers;
		std::map<std::string, twitCurl*> sessions;
		std::map<std::string, status> connectionState;
};

static void spectrum_sigchld_handler(int sig)
{
	int status;
	pid_t pid;

	do {
		pid = waitpid(-1, &status, WNOHANG);
	} while (pid != 0 && pid != (pid_t)-1);

	if ((pid == (pid_t) - 1) && (errno != ECHILD)) {
		char errmsg[BUFSIZ];
		snprintf(errmsg, BUFSIZ, "Warning: waitpid() returned %d", pid);
		perror(errmsg);
	}
}


int main (int argc, char* argv[]) {
	std::string host;
	int port;

	if (signal(SIGCHLD, spectrum_sigchld_handler) == SIG_ERR) {
		std::cout << "SIGCHLD handler can't be set\n";
		return -1;
	}

	boost::program_options::options_description desc("Usage: spectrum [OPTIONS] <config_file.cfg>\nAllowed options");
	desc.add_options()
		("host,h", value<std::string>(&host), "host")
		("port,p", value<int>(&port), "port")
		;
	try
	{
		boost::program_options::variables_map vm;
		boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
		boost::program_options::notify(vm);
	}
	catch (std::runtime_error& e)
	{
		std::cout << desc << "\n";
		exit(1);
	}
	catch (...)
	{
		std::cout << desc << "\n";
		exit(1);
	}


	if (argc < 5) {
		return 1;
	}

	Config config;
	if (!config.load(argv[5])) {
		std::cerr << "Can't open " << argv[1] << " configuration file.\n";
		return 1;
	}

	Logging::initBackendLogging(&config);

	Swift::SimpleEventLoop eventLoop;
	loop_ = &eventLoop;
	np = new TwitterPlugin(&config, &eventLoop, host, port);
	loop_->run();

	return 0;
}
