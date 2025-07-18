#include "utility.hpp"
#include <irods/rodsClient.h>
#include <irods/rodsError.h>
#include <irods/parseCommandLine.h>
#include <irods/rodsPath.h>
#include <irods/replUtil.h>
#include <irods/rcGlobalExtern.h>
#include <irods/irods_client_api_table.hpp>
#include <irods/irods_pack_table.hpp>

#include <cstdio>
#include <iostream>

void usage();

int
main( int argc, char **argv ) {

    signal( SIGPIPE, SIG_IGN );

    int status;
    rodsEnv myEnv;
    rErrMsg_t errMsg;
    rcComm_t *conn;
    rodsArguments_t myRodsArgs;
    char *optStr;
    rodsPathInp_t rodsPathInp;
    int reconnFlag;


    optStr = "aG:MN:hrvVn:PR:S:TX:UZ"; // JMC - backport 4549

    status = parseCmdLineOpt( argc, argv, optStr, 1, &myRodsArgs ); // JMC - backport 4549

    if ( status < 0 ) {
        printf( "Use -h for help.\n" );
        exit( 1 );
    }

    if ( myRodsArgs.help == True ) {
        usage();
        exit( 0 );
    }

    if ( argc - optind <= 0 ) {
        rodsLog( LOG_ERROR, "irepl: no input" );
        printf( "Use -h for help.\n" );
        exit( 2 );
    }

    status = getRodsEnv( &myEnv );

    if ( status < 0 ) {
        rodsLogError( LOG_ERROR, status, "main: getRodsEnv error. " );
        exit( 1 );
    }

    status = parseCmdLinePath( argc, argv, optind, &myEnv,
                               UNKNOWN_OBJ_T, NO_INPUT_T, 0, &rodsPathInp );

    if ( status < 0 ) {
        rodsLogError( LOG_ERROR, status, "main: parseCmdLinePath error. " );
        printf( "Use -h for help.\n" );
        exit( 1 );
    }

    if ( myRodsArgs.reconnect == True ) {
        reconnFlag = RECONN_TIMEOUT;
    }
    else {
        reconnFlag = NO_RECONN;
    }

    // =-=-=-=-=-=-=-
    // initialize pluggable api table
    irods::api_entry_table&  api_tbl = irods::get_client_api_table();
    irods::pack_entry_table& pk_tbl  = irods::get_pack_table();
    init_api_table( api_tbl, pk_tbl );

    conn = rcConnect( myEnv.rodsHost, myEnv.rodsPort, myEnv.rodsUserName,
                      myEnv.rodsZone, reconnFlag, &errMsg );

    if ( conn == NULL ) {
        exit( 2 );
    }

    status = utils::authenticate_client(conn, myEnv);
    if ( status != 0 ) {
        print_error_stack_to_file(conn->rError, stderr);
        rcDisconnect( conn );
        exit( 7 );
    }

    if ( myRodsArgs.progressFlag == True ) {
        gGuiProgressCB = ( guiProgressCallback ) iCommandProgStat;
    }

    status = replUtil( conn, &myEnv, &myRodsArgs, &rodsPathInp );

    printErrorStack( conn->rError );
    rcDisconnect( conn );

    if ( status < 0 ) {
        exit( 3 );
    }
    else {
        exit( 0 );
    }

}

void
usage() {

    char *msgs[] = {
        "Usage: irepl [-aMPrTvV] [-n replNum] [-R destResource] [-S srcResource]",
        "[-N numThreads] [-X restartFile] [--purgec] dataObj|collection ... ",
        " ",
        "Replicate a file in iRODS to another storage resource.",
        " ",
        "The -X option specifies that the restart option is on and the restartFile",
        "input specifies a local file that contains the restart info. If the ",
        "restartFile does not exist, it will be created and used for recording ",
        "subsequent restart info. If it exists and is not empty, the restart info",
        "contained in this file will be used for restarting the operation.",
        "Note that the restart operation only works for uploading directories and",
        "the path input must be identical to the one that generated the restart file",
        " ",
        "The -T option will renew the socket connection between the client and ",
        "server after 10 minutes of connection. This gets around the problem of",
        "sockets getting timed out by the firewall as reported by some users.",
        " ",
        "The -R option cannot be used to target a destination resource that",
        "is a child resource within a resource hierarchy.  Doing so will result",
        "in a DIRECT_CHILD_ACCESS error. Child resources are managed and their",
        "replication policy is handled by their hierarchy.",
        " ",
        "Note that if -a and -U options are used together, it means update all",
        "stale copies.",
        " ",
        "Note that if the source copy has a checksum value associated with it,",
        "a checksum will be computed for the replicated copy and compare with",
        "the source value for verification.",
        " ",
        "Note that replication is always within a zone.  For cross-zone duplication",
        "see irsync which can operate within a zone or across zones.",
        " ",
        "To specify a specific replica to use as a source for replication, use -n",
        "and indicate the replica by its replica number. If that replica cannot be",
        "used as a source for replication for any reason, replication will fail.",
        " ",
        "To specify that a replica in a specific resource hierarchy should be used",
        "as a source for replication, use -S and indicate the root resource of the",
        "hierarchy. If no replica in the hierarchy can be used as a source for",
        "replication for any reason, replication will fail.",
        " ",
        "-S and -n are incompatible options.",
        " ",
        "Options are:",
        " -a  all - if used with -U, update all stale copies",
        " -P  output the progress of the replication.",
        " -U  Update (Synchronize) an old replica with the latest copy. (see -a)",
        " -M  admin - admin user uses this option to backup/replicate other users files",
        " -N  number  specifies the number of I/O threads to use, by default a rule",
        "     is used to determine the best value.",
        " -r  recursive - copy the whole subtree",
        " -n  replNum - specifies the number of the source replica to use for replication.",
        " -R  destResource - specifies the destination resource to store to.",
        "     This can also be specified in your environment or via a rule set up",
        "     by the administrator.",
        " -S  srcResource - specifies the source resource of the data object to be",
        "     replicated. Must refer to a root resource.",
        " -T  renew socket connection after 10 minutes",
        " -v  verbose",
        " -V  Very verbose",
        " -X  restartFile - specifies that the restart option is on and the",
        "     restartFile input specifies a local file that contains the restart info.",
        " --purgec  Purge the staged cache copy after replicating an object to a",
        "     COMPOUND resource",
        " -h  this help",
        " ",
        "Also see 'irsync' for other types of iRODS/local synchronization.",
        ""
    };
    int i;
    for ( i = 0;; i++ ) {
        if ( strlen( msgs[i] ) == 0 ) {
            break;
        }
        printf( "%s\n", msgs[i] );
    }
    printReleaseInfo( "irepl" );
}
