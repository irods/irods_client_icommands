/*** Copyright (c), The Regents of the University of California            ***
 *** For more information please refer to files in the COPYRIGHT directory ***/

/*
  This is an interface to the Attribute-Value-Units type of metadata.
*/

#include "rods.h"
#include "rodsClient.h"
#include "rcMisc.h"
#include "irods_client_api_table.hpp"
#include "irods_pack_table.hpp"
#include "irods_query.hpp"
#include "user_administration.hpp"

#include <boost/algorithm/string/trim.hpp>
#include <boost/format.hpp>
#include <stdexcept>

#define MAX_SQL 300
#define BIG_STR 200

char cwd[BIG_STR];

int debug = 0;

rcComm_t *Conn {};
rodsEnv myEnv;

void usage();

auto genQuery_id_string_to_ulong(const std::string & s) -> unsigned long;

/*
 print the results of a general query.
 */
int
printGenQueryResults( rcComm_t *Conn, int status, genQueryOut_t *genQueryOut,
                      char *descriptions[], int formatFlag ) {
    int printCount;
    int i, j;
    char localTime[TIME_LEN];
    printCount = 0;
    if ( status != 0 ) {
        printError( Conn, status, "rcGenQuery" );
    }
    else {
        if ( status != CAT_NO_ROWS_FOUND ) {
            for ( i = 0; i < genQueryOut->rowCnt; i++ ) {
                if ( i > 0 && descriptions ) {
                    printf( "----\n" );
                }
                for ( j = 0; j < genQueryOut->attriCnt; j++ ) {
                    char *tResult;
                    tResult = genQueryOut->sqlResult[j].value;
                    tResult += i * genQueryOut->sqlResult[j].len;
                    if ( descriptions ) {
                        if ( strcmp( descriptions[j], "time" ) == 0 ) {
                            getLocalTimeFromRodsTime( tResult, localTime );
                            printf( "%s: %s : %s\n", descriptions[j], tResult,
                                    localTime );
                        }
                        else {
                            printf( "%s: %s\n", descriptions[j], tResult );
                        }
                    }
                    else {
                        if ( formatFlag == 0 ) {
                            printf( "%s\n", tResult );
                        }
                        else {
                            printf( "%s ", tResult );
                        }
                    }
                    printCount++;
                }
                if ( formatFlag == 1 ) {
                    printf( "\n" );
                }
            }
        }
    }
    return 0;
}

/*
Via a general query, show rule information
*/

namespace {
    int i1a[20];
    char v1[BIG_STR];
    char v2[BIG_STR];
    int status;
    int printCount = 0;
    char *columnNames[] = {"id", "name", "rei_file_path", "user_name",
                           "address", "time", "frequency", "priority",
                           "estimated_exe_time", "notification_addr",
                           "last_exe_time", "exec_status"
                          };


    struct once {
        int brief_format_len{};
        int long_format_len{};

        once()
        {
            int i = 0;
            i1a[i++] = COL_RULE_EXEC_ID;
            i1a[i++] = COL_RULE_EXEC_NAME;

            brief_format_len = i;

            i1a[i++] = COL_RULE_EXEC_REI_FILE_PATH;
            i1a[i++] = COL_RULE_EXEC_USER_NAME;
            i1a[i++] = COL_RULE_EXEC_ADDRESS;
            i1a[i++] = COL_RULE_EXEC_TIME;
            i1a[i++] = COL_RULE_EXEC_FREQUENCY;
            i1a[i++] = COL_RULE_EXEC_PRIORITY;
            i1a[i++] = COL_RULE_EXEC_ESTIMATED_EXE_TIME;
            i1a[i++] = COL_RULE_EXEC_NOTIFICATION_ADDR;
            i1a[i++] = COL_RULE_EXEC_LAST_EXE_TIME;
            i1a[i++] = COL_RULE_EXEC_STATUS;

            long_format_len = i;
        }
    }
    once_instance;
}

auto show_RuleExec( char *userName, 
                    const char *ruleName="", 
                    int allFlag = false, 
                    bool brief = false ) -> int;

int
main( int argc, char **argv ) {

    signal( SIGPIPE, SIG_IGN );

    int status, nArgs;
    rErrMsg_t errMsg;

    rodsArguments_t myRodsArgs;

    char userName[NAME_LEN];

    rodsLogLevel( LOG_ERROR );

    status = parseCmdLineOpt( argc, argv, "alu:vVh", 0, &myRodsArgs );
    if ( status ) {
        printf( "Use -h for help\n" );
        return 1;
    }
    if ( myRodsArgs.help == True ) {
        usage();
        return 0;
    }

    try {
        status = getRodsEnv( &myEnv );
        if ( status < 0 ) {
            rodsLog( LOG_ERROR, "main: getRodsEnv error. status = %d",
                     status );
            return 1;
        }

        if ( myRodsArgs.user ) {
            snprintf( userName, sizeof( userName ), "%s", myRodsArgs.userString );
        }
        else {
            snprintf( userName, sizeof( userName ), "%s", myEnv.rodsUserName );
        }

        // =-=-=-=-=-=-=-
        // initialize pluggable api table
        irods::api_entry_table&  api_tbl = irods::get_client_api_table();
        irods::pack_entry_table& pk_tbl  = irods::get_pack_table();
        init_api_table( api_tbl, pk_tbl );

        Conn = rcConnect( myEnv.rodsHost, myEnv.rodsPort, myEnv.rodsUserName,
                          myEnv.rodsZone, 0, &errMsg );

        if ( Conn == NULL ) {
            return 2;
        }

        status = clientLogin( Conn );
        if ( status != 0 ) {
            if ( !debug ) {
                return 3;
            }
        }

        if (nArgs = argc - myRodsArgs.optind; nArgs > 0) {
            static_cast<void>(genQuery_id_string_to_ulong( argv[myRodsArgs.optind]));
        }

        status = show_RuleExec( userName,
                               nArgs > 0 ? argv[myRodsArgs.optind] : "",
                               myRodsArgs.all,
                               myRodsArgs.longOption == 0 && nArgs == 0 );

        printErrorStack( Conn->rError );
    }
    catch (const irods::exception & e) {
        fprintf(stderr, "Error: %s\n", e.client_display_what());
        status = 1;
    }

    if (Conn) {
        rcDisconnect( Conn );
    }

    return status;
}

/*
Print the main usage/help information.
 */
void usage() {
    char *msgs[] = {
        "Usage: iqstat [-luvVh] [-u user] [ruleId]",
        "Show information about your pending iRODS rule executions",
        "or for the entered user.",
        " -a        display requests of all users",
        " -l        for long format",
        " -u user   for the specified user",
        " ruleId for the specified rule",
        " ",
        "See also iqdel and iqmod",
        ""
    };
    int i;
    for ( i = 0;; i++ ) {
        if ( strlen( msgs[i] ) == 0 ) {
            break;
        }
        printf( "%s\n", msgs[i] );
    }
    printReleaseInfo( "iqstat" );
}

auto show_RuleExec( char *userName, 
                    const char *ruleName, 
                    int allFlag, 
                    bool brief ) -> int
{
    genQueryInp_t genQueryInp{};
    genQueryOut_t *genQueryOut{};

    auto num_cols_selected = (brief ? once_instance.brief_format_len
                                    : once_instance.long_format_len);

    for (int i = 0; i < num_cols_selected; i++) {
        addInxIval(&genQueryInp.selectInp,i1a[i],0);
    }

    if (!allFlag) {
        snprintf( v1, BIG_STR, "='%s'", userName );
        addInxVal(&genQueryInp.sqlCondInp,COL_RULE_EXEC_USER_NAME,v1);
    }

    std::string diagnostic {""};
    if (ruleName != NULL && *ruleName != '\0') {
        diagnostic = (boost::format( " (and matching key '%s')" ) % ruleName).str();
        snprintf( v2, BIG_STR, "='%s'", ruleName );
        addInxVal(&genQueryInp.sqlCondInp,COL_RULE_EXEC_ID,v2);
    }

    genQueryInp.maxRows = 50;
    status = rcGenQuery( Conn, &genQueryInp, &genQueryOut );

    if ( status == CAT_NO_ROWS_FOUND ) {
        // Need to determine which table "no rows" refers to: USER or RULE.
        namespace ia = irods::experimental::administration;
        if (ia::client::exists(*Conn, ia::user{userName})) {
            if ( allFlag ) {
                printf( "No delayed rules pending%s\n", diagnostic.c_str() );
            }
            else {
                printf( "No delayed rules pending for user %s%s\n", userName, diagnostic.c_str());
            }
            return 0;
        }
        else {
            printf( "User %s does not exist.\n", userName );
            return 0;
        }
    }

    if (brief) {
        printf( "id     name\n" );
    }

    auto increment_count_and_print_results = [&] {
       printCount += printGenQueryResults( Conn, status, genQueryOut,
                                           brief ? NULL: columnNames,
                                           int{brief} );
    };

    increment_count_and_print_results();

    while ( status == 0 && genQueryOut->continueInx > 0 ) {
        genQueryInp.continueInx = genQueryOut->continueInx;
        status = rcGenQuery( Conn, &genQueryInp, &genQueryOut );
        if (!brief && genQueryOut->rowCnt > 0 ) {
            printf( "----\n" );
        }
        increment_count_and_print_results();
        clearGenQueryOut( genQueryOut );
    }
    clearGenQueryInp( &genQueryInp );
    return 0;
}

auto genQuery_id_string_to_ulong(const std::string & key) -> unsigned long
{
    std::string k{key};
    boost::algorithm::trim(k);
    try {
        return std::stoul(k);
    }
    catch(const std::invalid_argument&){
        THROW(SYS_INVALID_INPUT_PARAM, "Delayed task ID has incorrect format.");
    }
    catch(const std::out_of_range&){
        THROW(SYS_INVALID_INPUT_PARAM, "Delayed task ID is too large.");
    }
    catch(...){
        THROW(SYS_INVALID_INPUT_PARAM, "Unknown error parsing task ID.");
    }
}
