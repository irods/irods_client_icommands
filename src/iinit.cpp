#include <irods/authentication_plugin_framework.hpp>
#include <irods/irods_auth_constants.hpp>
#include <irods/irods_client_api_table.hpp>
#include <irods/irods_configuration_keywords.hpp>
#include <irods/irods_environment_properties.hpp>
#include <irods/irods_gsi_object.hpp>
#include <irods/irods_kvp_string_parser.hpp>
#include <irods/irods_native_auth_object.hpp>
#include <irods/irods_pack_table.hpp>
#include <irods/irods_pam_auth_object.hpp>
#include <irods/parseCommandLine.h>
#include <irods/rcConnect.h>
#include <irods/rcMisc.h>
#include <irods/rods.h>
#include <irods/rodsClient.h>
#include <irods/rodsError.h>

#include <boost/lexical_cast.hpp>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <iostream>

void usage( char *prog );

/* Uncomment the line below if you want TTL to be required for all
   users; i.e. all credentials will be time-limited.  This is only
   enforced on the client side so users can bypass this restriction by
   building their own iinit but it would strongly encourage the use of
   time-limited credentials. */
/* Uncomment the line below if you also want a default TTL if none
   is specified by the user. This TTL is specified in hours. */

#define TTYBUF_LEN 100
#define UPDATE_TEXT_LEN NAME_LEN*10

/*
 Attempt to make the ~/.irods directory in case it doesn't exist (may
 be needed to write the .irodsA file and perhaps the
 irods_environment.json file).
 */
int
mkrodsdir() {
    namespace fs = std::filesystem;

    const char* home_dir{std::getenv("HOME")};
    if (home_dir == nullptr) {
        fmt::print(stderr, "environment variable HOME not set\n");
        return -1;
    }

    fs::path irods_dir{home_dir};
    irods_dir /= ".irods";

    std::error_code err_code;
    fs::create_directory(irods_dir, err_code);
    if (err_code) {
        fmt::print(
            stderr,
            "failed to create directory [{}] with the following error: [{}]\n",
            irods_dir.string(), err_code.message());
        return -1;
    }

    fs::permissions(irods_dir, fs::perms::owner_all, err_code);
    if (err_code) {
        fmt::print(stderr,
                   "setting permissions for directory [{}] failed with the "
                   "following error: [{}]\n",
                   irods_dir.string(), err_code.message());
        return -1;
    }

    return 0;
}

namespace
{
    namespace irods_auth = irods::experimental::auth;

    constexpr const char* const AUTH_OPENID_SCHEME = "openid";
    constexpr const char* const PAM_INTERACTIVE_SCHEME = "pam_interactive";
    constexpr const char* const PAM_PASSWORD_SCHEME = "pam_password";

    auto scheme_uses_iinit_password_prompt(const std::string_view _scheme) -> bool
    {
        const std::initializer_list<const std::string_view> no_password_prompt = {
            AUTH_OPENID_SCHEME,
            irods::AUTH_GSI_SCHEME,
            irods::AUTH_PAM_SCHEME,
            PAM_PASSWORD_SCHEME,
            PAM_INTERACTIVE_SCHEME
        };

        return std::none_of(std::cbegin(no_password_prompt),
                            std::cend(no_password_prompt),
                            [&_scheme](const auto& _s) { return _scheme == _s; });
    } // scheme_uses_iinit_password_prompt

    auto save_updates_to_irods_environment(const nlohmann::json& _update) -> void
    {
        std::string env_file;
        std::string session_file;
        if (auto ret = irods::get_json_environment_file(env_file, session_file); !ret.ok()) {
            fmt::print("failed to get environment file - [{}]\n", ret.code());
        }

        json obj_to_dump;

        if (std::ifstream in{env_file}; in) {
            try {
                in >> obj_to_dump;
            }
            catch (const json::parse_error& e) {
                obj_to_dump = _update;
                fmt::print(
                    stderr,
                    "Failed to parse environment file: [{}]\n"
                    "Falling back to original environment settings.",
                    e.what());
            }

            obj_to_dump.merge_patch(_update);
        }
        else {
            obj_to_dump = _update;
        }

        if (std::ofstream f(env_file.c_str(), std::ios::out); f) {
            f << obj_to_dump.dump(4) << "\n";
        }
        else {
            fmt::print("Failed to save environment file [{}]\n", env_file);
        }
    } // save_updates_to_irods_environment

    auto option_specified(std::string_view _option, int argc, char** argv) -> bool
    {
        for (int arg = 0; arg < argc; ++arg) {
            if (!argv[arg]) {
                continue;
            }

            if (_option == argv[arg]) {
                // parseCmdLineOpt is EVIL and requires this. Please don't ask why.
                argv[arg] = "-Z";
                return true;
            }
        }

        return false;
    } // option_specified

    auto set_env_from_prompt(char _setting[], const char* _prompt, std::size_t _len) -> void
    {
        // If the setting already has a value, use that as the default value and show it in the prompt.
        if (0 == std::strlen(_setting)) {
            fmt::print("{}: ", _prompt);
        }
        else {
            fmt::print("{} [{}]: ", _prompt, _setting);
        }

        std::string response;
        std::getline(std::cin, response);

        if (!response.empty()) {
            std::strncpy(_setting, response.c_str(), _len);
        }
    } // set_env_from_prompt

    auto set_env_from_prompt(char _setting[], const char* _default, const char* _prompt, std::size_t _len) -> void
    {
        const bool env_has_value = 0 != std::strlen(_setting);
        const char* default_value = env_has_value ? _setting : _default;

        fmt::print("{} [{}]: ", _prompt, default_value);
        std::string response;
        std::getline(std::cin, response);

        if (!response.empty()) {
            std::strncpy(_setting, response.c_str(), _len);
        }
        else if (!env_has_value) {
            std::strncpy(_setting, default_value, _len);
        }
    } // set_env_from_prompt

    auto set_env_from_prompt(int& _setting, int _default, const char* _prompt) -> void
    {
        const bool env_has_value = 0 != _setting;
        const auto default_value = env_has_value ? _setting : _default;

        fmt::print("{} [{}]: ", _prompt, default_value);
        std::string response;
        std::getline(std::cin, response);

        if (!response.empty()) {
            try {
                _setting = boost::lexical_cast<int>(response);
            }
            catch (const boost::bad_lexical_cast&) {
                fmt::print("Entered value [{}] failed to convert to integer. Using [{}].\n", response, default_value);
                _setting = default_value;
            }
        }
        else if (!env_has_value) {
            _setting = default_value;
        }
    } // set_env_from_prompt

    auto configure_required_settings_in_env(RodsEnvironment& _env, nlohmann::json& _json_env) -> void
    {
        if (0 == std::strlen(_env.rodsHost)) {
            constexpr const char* host_prompt = "Enter the host name (DNS) of the server to connect to";
            set_env_from_prompt(_env.rodsHost, host_prompt, sizeof(_env.rodsHost));
            _json_env[irods::KW_CFG_IRODS_HOST] = _env.rodsHost;
        }

        if (0 == _env.rodsPort) {
            constexpr const char* port_prompt = "Enter the port number";
            constexpr int default_port = 1247;
            set_env_from_prompt(_env.rodsPort, default_port, port_prompt);
            _json_env[irods::KW_CFG_IRODS_PORT] = _env.rodsPort;
        }

        if (0 == std::strlen(_env.rodsUserName)) {
            constexpr const char* username_prompt = "Enter your iRODS user name";
            set_env_from_prompt(_env.rodsUserName, username_prompt, sizeof(_env.rodsUserName));
            _json_env[irods::KW_CFG_IRODS_USER_NAME] = _env.rodsUserName;
        }

        if (0 == std::strlen(_env.rodsZone)) {
            constexpr const char* zone_prompt = "Enter your iRODS zone";
            set_env_from_prompt(_env.rodsZone, zone_prompt, sizeof(_env.rodsZone));
            _json_env[irods::KW_CFG_IRODS_ZONE] = _env.rodsZone;
        }
    } // configure_required_settings_in_env

    auto configure_ssl_in_env(RodsEnvironment& _env, nlohmann::json& _json_env) -> void
    {
        // If the user indicated that SSL is going to be used, this setting is required, so no prompt is shown.
        constexpr const char* default_client_server_policy = "CS_NEG_REQUIRE";
        std::strncpy(_env.rodsClientServerPolicy, default_client_server_policy, sizeof(_env.rodsClientServerPolicy));
        _json_env[irods::KW_CFG_IRODS_CLIENT_SERVER_POLICY] = _env.rodsClientServerPolicy;

        // If the user indicated that SSL is going to be used, this setting is required, so no prompt is shown.
        constexpr const char* default_server_negotiation = "request_server_negotiation";
        std::strncpy(
            _env.rodsClientServerNegotiation, default_server_negotiation, sizeof(_env.rodsClientServerNegotiation));
        _json_env[irods::KW_CFG_IRODS_CLIENT_SERVER_NEGOTIATION] = _env.rodsClientServerNegotiation;

        constexpr const char* default_server_verification = "hostname";
        constexpr const char* server_verification_prompt = "Enter the server verification level";
        set_env_from_prompt(
            _env.irodsSSLVerifyServer,
            default_server_verification,
            server_verification_prompt,
            sizeof(_env.irodsSSLVerifyServer));
        _json_env[irods::KW_CFG_IRODS_SSL_VERIFY_SERVER] = _env.irodsSSLVerifyServer;

        constexpr const char* certificate_file_prompt = "Enter the full path to the CA certificate file";
        set_env_from_prompt(
            _env.irodsSSLCACertificateFile, certificate_file_prompt, sizeof(_env.irodsSSLCACertificateFile));
        _json_env[irods::KW_CFG_IRODS_SSL_CA_CERTIFICATE_FILE] = _env.irodsSSLCACertificateFile;
    } // configure_ssl_in_env

    auto configure_encryption_in_env(RodsEnvironment& _env, nlohmann::json& _json_env) -> void
    {
        constexpr const char* default_encryption_algorithm = "AES-256-CBC";
        constexpr const char* encryption_algorithm_prompt = "Enter the encryption algorithm";
        set_env_from_prompt(
            _env.rodsEncryptionAlgorithm,
            default_encryption_algorithm,
            encryption_algorithm_prompt,
            sizeof(_env.rodsEncryptionAlgorithm));
        _json_env[irods::KW_CFG_IRODS_ENCRYPTION_ALGORITHM] = _env.rodsEncryptionAlgorithm;

        constexpr int default_encryption_key_size = 32;
        constexpr const char* encryption_key_size_prompt = "Enter the encryption key size";
        set_env_from_prompt(_env.rodsEncryptionKeySize, default_encryption_key_size, encryption_key_size_prompt);
        _json_env[irods::KW_CFG_IRODS_ENCRYPTION_KEY_SIZE] = _env.rodsEncryptionKeySize;

        constexpr int default_encryption_salt_size = 8;
        constexpr const char* encryption_salt_size_prompt = "Enter the encryption salt size";
        set_env_from_prompt(_env.rodsEncryptionSaltSize, default_encryption_salt_size, encryption_salt_size_prompt);
        _json_env[irods::KW_CFG_IRODS_ENCRYPTION_SALT_SIZE] = _env.rodsEncryptionSaltSize;

        constexpr int default_encryption_num_hash_rounds = 16;
        constexpr const char* encryption_num_hash_rounds_prompt = "Enter the number of hash rounds";
        set_env_from_prompt(
            _env.rodsEncryptionNumHashRounds, default_encryption_num_hash_rounds, encryption_num_hash_rounds_prompt);
        _json_env[irods::KW_CFG_IRODS_ENCRYPTION_NUM_HASH_ROUNDS] = _env.rodsEncryptionNumHashRounds;
    } // configure_encryption_in_env
} // anonymous namespace

int main( int argc, char **argv )
{
    signal( SIGPIPE, SIG_IGN );

    int i = 0, status = 0;
    rodsEnv my_env;
    rcComm_t *Conn = 0;
    rErrMsg_t errMsg;
    rodsArguments_t myRodsArgs;

    // THESE MUST BE DONE HERE! parseCmdLineOpt is EVIL and considers any unknown options invalid.
    // TODO: use boost::program_options
    const auto configure_ssl = option_specified("--with-ssl", argc, argv);
    const auto prompt_auth_scheme = option_specified("--prompt-auth-scheme", argc, argv);

    status = parseCmdLineOpt( argc, argv, "hvVlZ", 1, &myRodsArgs );
    if ( status != 0 ) {
        printf( "Use -h for help.\n" );
        return 1;
    }

    if ( myRodsArgs.help == True ) {
        usage( argv[0] );
        return 0;
    }

    if ( myRodsArgs.longOption == True ) {
        printRodsEnv( stdout );
    }

    status = getRodsEnv( &my_env );
    if ( status < 0 ) {
        rodsLog( LOG_ERROR, "main: getRodsEnv error. status = %d",
                 status );
        return 1;
    }

    int ttl = 0;
    if ( myRodsArgs.ttl == True ) {
        ttl = myRodsArgs.ttlValue;
        if ( ttl < 1 ) {
            printf( "Time To Live value needs to be a positive integer\n" );
            return 1;
        }
    }

    if ( myRodsArgs.longOption == True ) {
        /* just list the env */
        return 0;
    }

    // Create ~/.irods/ if it does not exist
    if (mkrodsdir() != 0) {
        return 1;
    }

    using json = nlohmann::json;

    auto json_env = json::object();

    configure_required_settings_in_env(my_env, json_env);

    if (prompt_auth_scheme) {
        constexpr const char* default_auth_scheme = "native";
        constexpr const char* auth_scheme_prompt = "Enter your iRODS authentication scheme";
        set_env_from_prompt(
            my_env.rodsAuthScheme, default_auth_scheme, auth_scheme_prompt, sizeof(my_env.rodsAuthScheme));
        json_env[irods::KW_CFG_IRODS_AUTHENTICATION_SCHEME] = my_env.rodsAuthScheme;
    }

    if (configure_ssl) {
        configure_ssl_in_env(my_env, json_env);

        configure_encryption_in_env(my_env, json_env);
    }

    save_updates_to_irods_environment(json_env);
    _reloadRodsEnv(my_env);

    // =-=-=-=-=-=-=-
    // ensure scheme is lower case for comparison
    std::string lower_scheme = my_env.rodsAuthScheme;
    std::transform(
        lower_scheme.begin(),
        lower_scheme.end(),
        lower_scheme.begin(),
        ::tolower );

    if (irods::AUTH_GSI_SCHEME == lower_scheme) {
        printf( "Using GSI, attempting connection/authentication\n" );
    }

    if (std::string_view{ANONYMOUS_USER} != my_env.rodsUserName &&
        scheme_uses_iinit_password_prompt(lower_scheme)) {
        if ( myRodsArgs.verbose == True ) {
            i = obfSavePw( 0, 1, 1, nullptr );
        }
        else {
            i = obfSavePw( 0, 0, 0, nullptr );
        }

        if ( i != 0 ) {
            rodsLogError( LOG_ERROR, i, "Failed to save password." );
            return 1;
        }
    }

    // =-=-=-=-=-=-=-
    // initialize pluggable api table
    irods::api_entry_table&  api_tbl = irods::get_client_api_table();
    irods::pack_entry_table& pk_tbl  = irods::get_pack_table();
    init_api_table( api_tbl, pk_tbl );

    /* Connect... */
    Conn = rcConnect( my_env.rodsHost, my_env.rodsPort, my_env.rodsUserName,
                      my_env.rodsZone, 0, &errMsg );
    if ( Conn == NULL ) {
        rodsLog( LOG_ERROR,
                 "Saved password, but failed to connect to server %s",
                 my_env.rodsHost );
        return 2;
    }

    // =-=-=-=-=-=-=-
    // PAM auth gets special consideration, and also includes an
    // auth by the usual convention
    bool pam_flg = false;
    const auto use_legacy_authentication = irods_auth::use_legacy_authentication(*Conn);
    if (use_legacy_authentication && irods::AUTH_PAM_SCHEME == lower_scheme) {
        // =-=-=-=-=-=-=-
        // set a flag stating that we have done pam and the auth
        // scheme needs overridden
        pam_flg = true;

        // =-=-=-=-=-=-=-
        // build a context string which includes the ttl and password
        std::stringstream ttl_str;  ttl_str << ttl;
        irods::kvp_map_t ctx_map;
        ctx_map[irods::AUTH_TTL_KEY] = ttl_str.str();
        ctx_map[irods::AUTH_PASSWORD_KEY] = "";
        std::string ctx_str = irods::escaped_kvp_string(ctx_map);
        // =-=-=-=-=-=-=-
        // pass the context with the ttl as well as an override which
        // demands the pam authentication plugin
        status = clientLogin( Conn, ctx_str.c_str(), irods::AUTH_PAM_SCHEME.c_str() );
        if ( status != 0 ) {
            print_error_stack_to_file(Conn->rError, stderr);
            return 8;
        }

        // =-=-=-=-=-=-=-
        // if this succeeded, do the regular login below to check
        // that the generated password works properly.
    } // if pam

    if ( strcmp( my_env.rodsAuthScheme, AUTH_OPENID_SCHEME ) == 0 ) {
        irods::kvp_map_t ctx_map;
        try {
            std::string client_provider_cfg = irods::get_environment_property<std::string&>( "openid_provider" );
            ctx_map["provider"] = client_provider_cfg;
        }
        catch ( const irods::exception& e ) {
            if ( e.code() == KEY_NOT_FOUND ) {
                rodsLog( LOG_NOTICE, "KEY_NOT_FOUND: openid_provider not defined" );
            }
            else {
                rodsLog( LOG_DEBUG, "unknown error" );
                irods::log( e );
            }
        }
        ctx_map["nobuildctx"] = "1";
        ctx_map["reprompt"] = "1";

        std::string ctx_str = irods::escaped_kvp_string( ctx_map );
        status = clientLogin( Conn, ctx_str.c_str(), AUTH_OPENID_SCHEME );
        if ( status != 0 ) {
            print_error_stack_to_file(Conn->rError, stderr);
            rcDisconnect( Conn );
            return 7;
        }
    }
    else {
        if (use_legacy_authentication) {
            // =-=-=-=-=-=-=-
            // since we might be using PAM
            // and check that the user/password is OK
            const char* auth_scheme = ( pam_flg ) ?
                                  irods::AUTH_NATIVE_SCHEME.c_str() :
                                  my_env.rodsAuthScheme;
            status = clientLogin( Conn, 0, auth_scheme );
            if ( status != 0 ) {
                print_error_stack_to_file(Conn->rError, stderr);
                rcDisconnect( Conn );
                return 7;
            }

            printErrorStack( Conn->rError );
            if ( ttl > 0 && !pam_flg ) {
                /* if doing non-PAM TTL, now get the
                short-term password (after initial login) */
                status = clientLoginTTL( Conn, ttl );
                if ( status != 0 ) {
                    print_error_stack_to_file(Conn->rError, stderr);
                    rcDisconnect( Conn );
                    return 8;
                }
                /* And check that it works */
                status = clientLogin( Conn );
                if ( status != 0 ) {
                    print_error_stack_to_file(Conn->rError, stderr);
                    rcDisconnect( Conn );
                    return 7;
                }
            }
        }
        else {
            auto ctx = nlohmann::json{
                {irods::AUTH_TTL_KEY, std::to_string(ttl)},
                {irods_auth::force_password_prompt, true}
            };

            // Use the scheme override here to ensure that the authentication scheme in the environment is the same as
            // the authentication scheme configured here. If the scheme in the environment and the scheme configured in
            // iinit match, then nothing will need to change in clientLogin. If they do not match, the override wins.
            if (const int ec = clientLogin(Conn, ctx.dump().c_str(), my_env.rodsAuthScheme); ec != 0) {
                print_error_stack_to_file(Conn->rError, stderr);
                rcDisconnect(Conn);
                return 7;
            }

            printErrorStack(Conn->rError);
            if (ttl > 0 && lower_scheme != PAM_INTERACTIVE_SCHEME && lower_scheme != PAM_PASSWORD_SCHEME) {
                status = clientLoginTTL(Conn, ttl);
                if (status) {
                    print_error_stack_to_file(Conn->rError, stderr);
                    rcDisconnect(Conn);
                    return 8;
                }
                /* And check that it works */
                status = clientLogin(Conn);
                if (status) {
                    print_error_stack_to_file(Conn->rError, stderr);
                    rcDisconnect(Conn);
                    return 7;
                }
            }
        }
    }

    rcDisconnect( Conn );

    return 0;
} // main


void usage( char *prog ) {
    printf( "Creates a file containing your iRODS password in a scrambled form,\n" );
    printf( "to be used automatically by the icommands.\n" );
    printf( "\n" );
    printf( "Usage: %s [-hvVl] [--ttl TTL] [--with-ssl] [--prompt-auth-scheme]\n", prog );
    printf( "\n" );
    printf( "iinit loads environment information from the following locations, with\n" );
    printf( "priority being given to the top of the list:\n" );
    printf( "   - in specific environment variables\n" );
    printf( "   - in an irods_environment.json file located at IRODS_ENVIRONMENT_FILE\n" );
    printf( "   - in ~/.irods/irods_environment.json\n" );
    printf( "   - default values set in the server\n" );
    printf( "The active client environment file will be updated each time iinit is run in\n" );
    printf( "order to ensure that the settings are applied properly when connecting to the\n" );
    printf( "server.\n" );
    printf( "\n" );
    printf( "If any setting from the minimal client environment is found to be\n" );
    printf( "missing, prompts will be presented to the user to retrieve the missing\n" );
    printf( "required configurations. The 4 required configuration values are:\n" );
    printf( "  irods_host\n" );
    printf( "  irods_port\n" );
    printf( "  irods_user_name\n" );
    printf( "  irods_zone_name\n" );
    printf( "\n" );
    printf( "Finally, iinit will prompt the user for a password for some plugins.\n" );
    printf( "For an automated environment, the password can be piped to stdin like so:\n" );
    printf( "  $ echo $MY_IRODS_PASSWORD | iinit\n" );
    printf( "Of course, if there are missing client environment configuration values,\n" );
    printf( "these will need to be addressed in the piped input first.\n" );
    printf( "\n" );
    printf( "When using regular iRODS passwords you can use --ttl (Time To Live)\n" );
    printf( "to request a credential (a temporary password) that will be valid\n" );
    printf( "for only the number of hours you specify (up to a limit set by the\n" );
    printf( "administrator).  This is more secure, as this temporary password\n" );
    printf( "(not your permanent one) will be stored in the obfuscated\n" );
    printf( "credential file (.irodsA) for use by the other iCommands.\n" );
    printf( "\n" );
    printf( "When using PAM, iinit always generates a temporary iRODS password\n" );
    printf( "for use by the other iCommands, using a time-limit set by the\n" );
    printf( "administrator (usually a few days).  With the --ttl option, you can\n" );
    printf( "specify how long this derived password will be valid, within the\n" );
    printf( "limits set by the administrator.\n" );
    printf( "\n" );
    printf( "Options:\n" );
    printf( " -l  list the iRODS environment variables (only)\n" );
    printf( " -v  verbose\n" );
    printf( " -V  Very verbose\n" );
    printf( " --ttl TTL\n" );
    printf( "     set the password Time To Live (specified in hours)\n" );
    printf(" --with-ssl\n");
    printf("      Include prompts which will set up SSL communications in the\n");
    printf("      client environment.\n");
    printf(" --prompt-auth-scheme\n");
    printf("      Include a prompt to select the authentication scheme. If not specified\n");
    printf("      and no active client environment file exists, the default is 'native'.\n");
    printf( " -h  this help\n" );
    printReleaseInfo( "iinit" );
}
