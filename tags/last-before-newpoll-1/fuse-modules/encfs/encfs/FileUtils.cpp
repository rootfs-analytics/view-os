/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 *
 *****************************************************************************
 * Copyright (c) 2004, Valient Gough
 * 
 * This program is free software; you can distribute it and/or modify it under 
 * the terms of the GNU General Public License (GPL), as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

// defines needed for RedHat 7.3...
#ifdef linux
#define _XOPEN_SOURCE 500 // make sure pwrite() is pulled in
#endif
#define _BSD_SOURCE // pick up setenv on RH7.3

#include "encfs.h"
#include "config.h"

#include "readpassphrase.h"
#include "autosprintf.h"

#include "FileUtils.h"
#include "Config.h"

#include "DirNode.h"
#include "Cipher.h"
#include "StreamNameIO.h"
#include "BlockNameIO.h"

#include <rlog/rlog.h>
#include <rlog/Error.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <iostream>
#include <sstream>

#include "i18n.h"

using namespace rel;
using namespace rlog;
using namespace std;
using namespace gnu;

static const int DefaultBlockSize = 512;
// environment variable names for values encfs stores in the environment when
// calling an external password program.
static const char ENCFS_ENV_ROOTDIR[] = "encfs_root";
static const char ENCFS_ENV_STDOUT[] = "encfs_stdout";
static const char ENCFS_ENV_STDERR[] = "encfs_stderr";


//static int V5SubVersion = 20040518;
//static int V5SubVersion = 20040621; // add external IV chaining
static int V5SubVersion = 20040813; // fix MACFileIO block size issues
static int V5SubVersionDefault = 0;

struct ConfigInfo
{
    const char *fileName;
    ConfigType type;
    const char *environmentOverride;
    bool (*loadFunc)(const char *fileName, EncFSConfig *config,
	    ConfigInfo *cfg);
    bool (*saveFunc)(const char *fileName, EncFSConfig *config);
    int currentSubVersion;
    int defaultSubVersion;
} ConfigFileMapping[] = {
    {".encfs5", Config_V5, "ENCFS5_CONFIG", readV5Config, writeV5Config, 
	V5SubVersion, V5SubVersionDefault },
    // backward compatible support for config versions 3 and 4
    {".encfs4", Config_V4, NULL, readV4Config, writeV4Config, 0, 0 },
    {".encfs3", Config_V3, NULL, readV3Config, writeV3Config, 0, 0 },
    // forget about version 2 and 1 - from before the first public release!
    {".encfs2", Config_Prehistoric, NULL, NULL, NULL, 0, 0 },
    {".encfs", Config_Prehistoric, NULL, NULL, NULL, 0, 0 },
    {NULL,Config_None, NULL, NULL, NULL, 0, 0}};


EncFS_Root::EncFS_Root()
{
}

EncFS_Root::~EncFS_Root()
{
}


bool fileExists( const char * fileName )
{
    struct stat buf;
    if(!lstat( fileName, &buf ))
    {
	return true;
    } else
    {
	// XXX show perror?
	return false;
    }
}

bool isDirectory( const char *fileName )
{
    struct stat buf;
    if( !lstat( fileName, &buf ))
    {
	return S_ISDIR( buf.st_mode );
    } else
    {
	return false;
    }
}

bool isAbsolutePath( const char *fileName )
{
    if(fileName && fileName[0] != '\0' && fileName[0] == '/')
	return true;
    else
	return false;
}

const char *lastPathElement( const char *name )
{
    const char *loc = strrchr( name, '/' );
    return loc ? loc + 1 : name;
}

bool userAllowMkdir( const char *path, mode_t mode )
{
    // TODO: can we internationalize the y/n names?  Seems strange to prompt in
    // their own language but then have to respond 'y' or 'n'.
    // xgroup(setup)
    cerr << autosprintf( _("The directory \"%s\" does not exist. Should it be created? (y,n) "), path );
    char answer[10];
    fgets( answer, sizeof(answer), stdin );

    if(toupper(answer[0]) == 'Y')
    {
	int result = mkdir( path, mode );
	if(result < 0)
	{
	    perror( _("Unable to create directory: ") );
	    return false;
	} else
	    return true;
    } else
    {
	// Directory not created, by user request
	cerr << _("Directory not created.") << "\n";
	return false;
    }
}

ConfigType readConfig_load( ConfigInfo *nm, const char *path, 
	EncFSConfig *config )
{
    if( nm->loadFunc )
    {
	try
	{
	    if( (*nm->loadFunc)( path, config, nm ))
		return nm->type;
	} catch( rlog::Error & err )
	{
	    err.log( _RLWarningChannel );
	}

	rError( _("Found config file %s, but failed to load"), path);
	return Config_None;
    } else
    {
	// No load function - must be an unsupported type..
	return nm->type;
    }
}

ConfigType readConfig( const string &rootDir, EncFSConfig *config )
{
    ConfigInfo *nm = ConfigFileMapping;
    while(nm->fileName)
    {
	// allow environment variable to override default config path
	if( nm->environmentOverride != NULL )
	{
	    char *envFile = getenv( nm->environmentOverride );
	    if( envFile != NULL )
		return readConfig_load( nm, envFile, config );
	}
	// the standard place to look is in the root directory
	string path = rootDir + nm->fileName;
	if( fileExists( path.c_str() ) )
	    return readConfig_load( nm, path.c_str(), config);

	++nm;
    }

    return Config_None;
}

bool readV5Config( const char *configFile, EncFSConfig *config,
	ConfigInfo *info)
{
    bool ok = false;

    // use Config to parse the file and query it..
    Config cfgRdr;
    if(cfgRdr.load( configFile ))
    {
	try
	{
	    config->subVersion = cfgRdr["subVersion"].readBER( 
		    info->defaultSubVersion );
	    if(config->subVersion > info->currentSubVersion)
	    {
		/* config file specifies a version outside our supported
		 range..   */
		rWarning(_("Config subversion %i found, but this version of"
			" encfs only supports up to version %i."),
			config->subVersion, info->currentSubVersion);
		return false;
	    }
	    cfgRdr["creator"] >> config->creator;
	    cfgRdr["cipher"] >> config->cipherIface;
	    cfgRdr["naming"] >> config->nameIface;
	    cfgRdr["keySize"] >> config->keySize;
	    cfgRdr["blockSize"] >> config->blockSize;
	    cfgRdr["keyData"] >> config->keyData;
	    config->uniqueIV = cfgRdr["uniqueIV"].readBool( false );
	    config->chainedNameIV = cfgRdr["chainedIV"].readBool( false );
	    config->externalIVChaining = cfgRdr["externalIV"].readBool( false );
	    if( config->subVersion < 20040518 )
	    {
		bool blockMAC = cfgRdr["blockMAC"].readBool( false );
		if(blockMAC)
		{
		    config->blockMACBytes = 8;
		    config->blockMACRandBytes = 4;
		} else
		{
		    config->blockMACBytes = 0;
		    config->blockMACRandBytes = 0;
		}
	    } else
	    {
		config->blockMACBytes = cfgRdr["blockMACBytes"].readBER(0);
		config->blockMACRandBytes = 
		    cfgRdr["blockMACRandBytes"].readBER(0);
	    }

	    ok = true;
	} catch( rlog::Error &err)
	{
	    err.log( _RLWarningChannel );
	    rDebug("Error parsing data in config file %s", configFile);
	    ok = false;
	}
    }

    return ok;
}

bool readV4Config( const char *configFile, EncFSConfig *config,
	ConfigInfo *info)
{
    bool ok = false;

    // use Config to parse the file and query it..
    Config cfgRdr;
    if(cfgRdr.load( configFile ))
    {
	try
	{
	    cfgRdr["cipher"] >> config->cipherIface;
	    cfgRdr["keySize"] >> config->keySize;
	    cfgRdr["blockSize"] >> config->blockSize;
	    cfgRdr["keyData"] >> config->keyData;

	    // fill in default for V4
	    config->nameIface = Interface("nameio/stream", 1, 0, 0);
	    config->creator = "EncFS 1.0.x";
	    config->subVersion = info->defaultSubVersion;
	    config->blockMACBytes = 0;
	    config->blockMACRandBytes = 0;
	    config->uniqueIV = false;
	    config->externalIVChaining = false;
	    config->chainedNameIV = false;

	    ok = true;
	} catch( rlog::Error &err)
	{
	    err.log( _RLWarningChannel );
	    rDebug("Error parsing config file %s", configFile);
	    ok = false;
	}
    }

    return ok;
}

bool readV3Config( const char *configFile, EncFSConfig *config,
	ConfigInfo *info)
{
    (void)configFile;
    // use Config to parse the file and query it..
    // fill in default for V3
    config->creator = "EncFS 0.x";
    config->subVersion = info->defaultSubVersion;
    config->cipherIface = Interface("ssl/blowfish-v0.2", 1, 0, 0);
    config->nameIface = Interface("nameio/stream", 0, 1, 0);
    config->keySize = 160;
    config->blockSize = 64;
    config->blockMACBytes = 0;
    config->blockMACRandBytes = 0;
    config->uniqueIV = false;
    config->externalIVChaining = false;
    config->chainedNameIV = false;

    bool ok = false;
    int vkeyFD = open( configFile, O_RDONLY );
    if( vkeyFD >= 0 )
    {
	const int headerSize = 22; // 160 bit (20 bytes) + 2 byte checksum
	char keyBuf [ headerSize ];

	read( vkeyFD, keyBuf, headerSize );
	close( vkeyFD );

	config->keyData.assign( keyBuf, headerSize );
	ok = true;
    } else
    {
	rDebug("Error opening config file %s", configFile );
    }

    return ok;
}

bool saveConfig( ConfigType type, const string &rootDir,
	EncFSConfig *config )
{
    bool ok = false;

    ConfigInfo *nm = ConfigFileMapping;
    while(nm->fileName)
    {
	if( nm->type == type && nm->saveFunc )
	{
	    string path = rootDir + nm->fileName;
	    if( nm->environmentOverride != NULL )
	    {
		// use environment file if specified..
		const char *envFile = getenv( nm->environmentOverride );
		if( envFile != NULL )
		    path.assign( envFile );
	    }

	    try
	    {
		ok = (*nm->saveFunc)( path.c_str(), config );
	    } catch( rlog::Error &err )
	    {
		err.log( _RLWarningChannel );
		ok = false;
	    }
	    break;
	}
	++nm;
    }

    return ok;
}

bool writeV5Config( const char *configFile, EncFSConfig *config )
{
    Config cfg;

    cfg["creator"] << config->creator;
    cfg["subVersion"] << config->subVersion;
    cfg["cipher"] << config->cipherIface;
    cfg["naming"] << config->nameIface;
    cfg["keySize"] << config->keySize;
    cfg["blockSize"] << config->blockSize;
    cfg["keyData"] << config->keyData;
    cfg["blockMACBytes"] << config->blockMACBytes;
    cfg["blockMACRandBytes"] << config->blockMACRandBytes;
    cfg["uniqueIV"] << config->uniqueIV;
    cfg["chainedIV"] << config->chainedNameIV;
    cfg["externalIV"] << config->externalIVChaining;

    return cfg.save( configFile );
}

bool writeV4Config( const char *configFile, EncFSConfig *config )
{
    Config cfg;

    cfg["cipher"] << config->cipherIface;
    cfg["keySize"] << config->keySize;
    cfg["blockSize"] << config->blockSize;
    cfg["keyData"] << config->keyData;

    return cfg.save( configFile );
}

bool writeV3Config( const char *configFile, EncFSConfig *config )
{
    bool ok = true;

    int fd = open( configFile, O_RDWR );
    if(fd >= 0)
    {
	::pwrite(fd, config->keyData.data(), config->keyData.length(), 0);
	close( fd );
    }  else
    {
	rError(_("Error opening key file %s for write: %s"), configFile,
		strerror( errno ));
	ok = false;
    }

    return ok;
}

static
Cipher::CipherAlgorithm findCipherAlgorithm(const char *name,
	int keySize )
{
    Cipher::AlgorithmList algorithms = Cipher::GetAlgorithmList();
    Cipher::AlgorithmList::const_iterator it;
    for(it = algorithms.begin(); it != algorithms.end(); ++it)
    {
	if( !strcmp( name, it->name.c_str() )
		&& it->keyLength.allowed( keySize ))
	{
	    return *it;
	}
    }

    Cipher::CipherAlgorithm result;
    return result;
}

static
Cipher::CipherAlgorithm selectCipherAlgorithm()
{
    for(;;)
    {
	// figure out what cipher they want to use..
	// xgroup(setup)
	cout << _("The following cipher algorithms are available:") << "\n";
	Cipher::AlgorithmList algorithms = Cipher::GetAlgorithmList();
	Cipher::AlgorithmList::const_iterator it;
	int optNum = 1;
	for(it = algorithms.begin(); it != algorithms.end(); ++it, ++optNum)
	{
	    cout << optNum << ". " << it->name
		<< " : " << gettext(it->description.c_str()) << "\n";
	    if(it->keyLength.min() == it->keyLength.max())
	    {
		// shown after algorithm name and description.
		// xgroup(setup)
		cout << autosprintf(_(" -- key length %i bits")
			, it->keyLength.min()) << "\n";
	    } else
	    {
		cout << autosprintf(
			// shown after algorithm name and description.
			// xgroup(setup)
			_(" -- Supports key lengths of %i to %i bits"),
			it->keyLength.min(), it->keyLength.max()) << "\n";
	    }

	    if(it->blockSize.min() == it->blockSize.max())
	    {
		cout << autosprintf(
			// shown after algorithm name and description.
			// xgroup(setup)
			_(" -- block size %i bytes"), it->blockSize.min()) 
	    	    << "\n";
	    } else
	    {
		cout << autosprintf(
			// shown after algorithm name and description.
			// xgroup(setup)
			_(" -- Supports block sizes of %i to %i bytes"),
			it->blockSize.min(), it->blockSize.max()) << "\n";
	    }
	}

	// xgroup(setup)
	cout << "\n" << _("Enter the number corresponding to your choice: ");
	char answer[10];
	fgets( answer, sizeof(answer), stdin );
	int cipherNum = atoi( answer );
	cout << "\n";

	if( cipherNum < 1 || cipherNum > (int)algorithms.size() )
	{
	    cerr << _("Invalid selection.") << "\n";
	    continue;
	}

	it = algorithms.begin();
	while(--cipherNum) // numbering starts at 1
	    ++it;

	Cipher::CipherAlgorithm alg = *it;
       	
	// xgroup(setup)
	cout << autosprintf(_("Selected algorithm \"%s\""), alg.name.c_str()) 
	    << "\n\n";

	return alg;
    }
}

static
Interface selectNameCoding()
{
    for(;;)
    {
	// figure out what cipher they want to use..
	// xgroup(setup)
	cout << _("The following filename encoding algorithms are available:")
		<< "\n";
	NameIO::AlgorithmList algorithms = NameIO::GetAlgorithmList();
	NameIO::AlgorithmList::const_iterator it;
	int optNum = 1;
	for(it = algorithms.begin(); it != algorithms.end(); ++it, ++optNum)
	{
	    cout << optNum << ". " << it->name
		<< " : " << gettext(it->description.c_str()) << "\n";
	}

	// xgroup(setup)
	cout << "\n" << _("Enter the number corresponding to your choice: ");
	char answer[10];
	fgets( answer, sizeof(answer), stdin );
	int algNum = atoi( answer );
	cout << "\n";

	if( algNum < 1 || algNum > (int)algorithms.size() )
	{
	    cerr << _("Invalid selection.") << "\n";
	    continue;
	}

	it = algorithms.begin();
	while(--algNum) // numbering starts at 1
	    ++it;

	// xgroup(setup)
	cout << autosprintf(_("Selected algorithm \"%s\""), it->name.c_str()) 
	    << "\"\n\n";

	return it->iface;
    }
}

static
int selectKeySize( const Cipher::CipherAlgorithm &alg )
{
    if(alg.keyLength.min() == alg.keyLength.max())
    {
	cout << autosprintf(_("Using key size of %i bits"), 
		alg.keyLength.min()) << "\n";
	return alg.keyLength.min();
    }

    cout << autosprintf(
	// xgroup(setup)
	_("Please select a key size in bits.  The cipher you have chosen\n"
	  "supports sizes from %i to %i bits in increments of %i bits.\n"
	  "For example: "), alg.keyLength.min(), alg.keyLength.max(), 
	  alg.keyLength.inc()) << "\n";

    int numAvail = (alg.keyLength.max() - alg.keyLength.min()) 
	/ alg.keyLength.inc();

    if(numAvail < 5)
    {
	// show them all
	for(int i=0; i<=numAvail; ++i)
	{
	    if(i) 
		cout << ", ";
	    cout << alg.keyLength.min() + i * alg.keyLength.inc();
	}
    } else
    {
	// partial
	for(int i=0; i<3; ++i)
	{
	    if(i) 
		cout << ", ";
	    cout << alg.keyLength.min() + i * alg.keyLength.inc();
	}
	cout << " ... " << alg.keyLength.max() - alg.keyLength.inc();
	cout << ", " << alg.keyLength.max();
    }
    // xgroup(setup)
    cout << "\n" << _("Selected key size: ");

    char answer[10];
    fgets( answer, sizeof(answer), stdin );
    int keySize = atoi( answer );
    cout << "\n";

    keySize = alg.keyLength.closest( keySize );

    // xgroup(setup)
    cout << autosprintf(_("Using key size of %i bits"), keySize) << "\n\n";

    return keySize;
}

static
int selectBlockSize( const Cipher::CipherAlgorithm &alg )
{
    if(alg.blockSize.min() == alg.blockSize.max())
    {
	cout << autosprintf(
		// xgroup(setup)
		_("Using filesystem block size of %i bytes"),
		alg.blockSize.min()) << "\n";
	return alg.blockSize.min();
    }

    cout << autosprintf(
	    // xgroup(setup)
	    _("Select a block size in bytes.  The cipher you have chosen\n"
	      "supports sizes from %i to %i bytes in increments of %i.\n"
	      "Or just hit enter for the default (%i bytes)\n"),
	      alg.blockSize.min(), alg.blockSize.max(), alg.blockSize.inc(),
	      DefaultBlockSize);

    // xgroup(setup)
    cout << "\n" << _("filesystem block size: ");
	
    int blockSize = DefaultBlockSize;
    char answer[10];
    fgets( answer, sizeof(answer), stdin );
    cout << "\n";

    if( atoi( answer )  >= alg.blockSize.min() )
	blockSize = atoi( answer );

    blockSize = alg.blockSize.closest( blockSize );

    // xgroup(setup)
    cout << autosprintf(_("Using filesystem block size of %i bytes"), 
	    blockSize) << "\n\n";

    return blockSize;
}

static 
void selectBlockMAC(int *macBytes, int *macRandBytes)
{
    // xgroup(setup)
    cout << _("Enable block authentication code headers\n"
	"on every block in a file?  This adds about 12 bytes per block\n"
	"to the storage requirements for a file, and significantly affects\n"
	"performance but it also means [almost] any modifications or errors\n"
	"within a block will be caught and will cause a read error.\n"
	"The default here is No.  \n"
	"Any response that does not begin with 'y' will mean No: ");

    char answer[10];
    fgets( answer, sizeof(answer), stdin );
    cout << "\n";

    if(tolower(answer[0]) == 'y')
    {
	*macBytes = 8;

	// xgroup(setup)
	cout << _("Add random bytes to each block header?\n"
	    "This adds a performance penalty, but ensures that blocks\n"
	    "have different authentication codes.  Note that you can\n"
	    "have the same benefits by enabling per-file initialization\n"
	    "vectors, which does not come with as great of performance\n"
	    "penalty. \n"
	    "Select a number of bytes, from 0 (no random bytes) to 8: ");
    
	int randSize = 0;
	fgets( answer, sizeof(answer), stdin );
	cout << "\n";

	randSize = atoi( answer );
	if(randSize < 0)
	    randSize = 0;
	if(randSize > 8)
	    randSize = 8;

	*macRandBytes = randSize;
    } else
    {
	*macBytes = 0;
	*macRandBytes = 0;
    }
}

static 
bool selectUniqueIV()
{
    // xgroup(setup)
    cout << _("Enable per-file initialization vectors?\n"
	"This adds about 8 bytes per file to the storage requirements.\n"
	"It should not affect performance except possibly with applications\n"
	"which rely on block-aligned file io for performance.\n"
	"The default here is Yes.  \n"
	"Any response that does not begin with 'n' will mean Yes: ");

    char answer[10];
    fgets( answer, sizeof(answer), stdin );
    cout << "\n";

    if(tolower(answer[0]) == 'n')
	return false;
    else
	return true;
}

static 
bool selectChainedIV()
{
    // xgroup(setup)
    cout << _("Enable filename initialization vector chaining?\n"
	"This makes filename encoding dependent on the complete path, \n"
	"rather then encoding each path element individually. \n"
	"This is normally desireable, therefor the default is Yes. \n"
	"Any response that does not begin with 'n' will mean Yes: ");

    char answer[10];
    fgets( answer, sizeof(answer), stdin );
    cout << "\n";

    if(tolower(answer[0]) == 'n')
	return false;
    else
	return true;
}

static 
bool selectExternalChainedIV()
{
    // xgroup(setup)
    cout << _("Enable filename to IV header chaining?\n"
	"This makes file data encoding dependent on the complete file path.\n"
	"If a file is renamed, it will not decode sucessfully unless it\n"
	"was renamed by encfs with the proper key.\n"
	"If this option is enabled, then hard links will not be supported\n"
	"in the filesystem.\n"
	"The default is No. \n"
	"Any response that does not begin with 'y' will mean No: ");

    char answer[10];
    fgets( answer, sizeof(answer), stdin );
    cout << "\n";

    if(tolower(answer[0]) == 'y')
	return true;
    else
	return false;
}

RootPtr createV5Config( const std::string &rootDir, 
	bool enableIdleTracking, bool forceDecode,
	const std::string &passwordProgram,
	bool useStdin )
{
    RootPtr rootInfo;

    // creating new volume key.. should check that is what the user is
    // expecting...
    // xgroup(setup)
    cout << _("Creating new encrypted volume.") << endl;

    // xgroup(setup)
    cout << _("Please choose from one of the following options:\n"
              " enter \"x\" for expert configuration mode,\n"
              " enter \"p\" for pre-configured paranoia mode,\n"
              " anything else, or an empty line will select standard mode.\n"
              "?> ");
    
    char answer[10] = {0};
    fgets( answer, sizeof(answer), stdin );
    cout << "\n";

    int keySize = 0;
    int blockSize = 0;
    Cipher::CipherAlgorithm alg;
    Interface nameIOIface;
    int blockMACBytes = 0;
    int blockMACRandBytes = 0;
    bool uniqueIV = false;
    bool chainedIV = false;
    bool externalIV = false;

    if(answer[0] == 'p')
    {
	// xgroup(setup)
	cout << _("Paranoia configuration selected.") << "\n";
	// look for AES with 256 bit key..
	// Use block filename encryption mode.
	// Enable per-block HMAC headers at substantial performance penalty..
	// Enable per-file initialization vector headers.
	// Enable filename initialization vector chaning
	keySize = 256;
	blockSize = DefaultBlockSize;
	alg = findCipherAlgorithm("AES", keySize);
	nameIOIface = BlockNameIO::CurrentInterface();
	blockMACBytes = 8;
	blockMACRandBytes = 0; // using uniqueIV, so this isn't necessary
	uniqueIV = true;
	chainedIV = true;
	externalIV = true;
    } else
    if(answer[0] != 'x')
    {
	// xgroup(setup)
	cout << _("Standard configuration selected.") << "\n";
	// look for blowfish with 160 bit key..
	// block name encoding is now standard as well..
	// per-file initialization vectors are now standard, as they shouldn't
	// have any significant performance penalty.
	keySize = 160;
	blockSize = DefaultBlockSize;
	alg = findCipherAlgorithm("Blowfish", keySize);
	nameIOIface = BlockNameIO::CurrentInterface();
	blockMACBytes = 0;
	uniqueIV = true;
	chainedIV = true;
	externalIV = false;
    }

    if(answer[0] == 'x' || alg.name.empty())
    {
	if(answer[0] != 'x')
	{
	    // xgroup(setup)
	    cout << _("Sorry, unable to locate cipher for predefined "
		"configuration...\n"
		"Falling through to Manual configuration mode.");
	} else
	{
	    // xgroup(setup)
	    cout << _("Manual configuration mode selected.");
	}
	cout << endl;

	// query user for settings..
    	alg = selectCipherAlgorithm();
	keySize = selectKeySize( alg );
	blockSize = selectBlockSize( alg );
	nameIOIface = selectNameCoding();
	chainedIV = selectChainedIV();
	uniqueIV = selectUniqueIV();
	if(chainedIV && uniqueIV)
	    externalIV = selectExternalChainedIV();
	else
	{
	    // xgroup(setup)
	    cout << _("External chained IV disabled, as both 'IV chaining'\n"
		"and 'unique IV' features are required for this option.") 
		<< endl;
	    externalIV = false;
	}
	selectBlockMAC(&blockMACBytes, &blockMACRandBytes);
    }

    Ptr<Cipher> cipher = Cipher::New( alg.name, keySize );
    if(cipher.isNull())
    {
	rError(_("Unable to instanciate cipher %s, key size %i, block size %i"),
		alg.name.c_str(), keySize, blockSize);
	return rootInfo;
    } else
    {
	rDebug("Using cipher %s, key size %i, block size %i",
	    alg.name.c_str(), keySize, blockSize);
    }
    
    EncFSConfig config;

    config.cipherIface = cipher->interface();
    config.keySize = keySize;
    config.blockSize = blockSize;
    config.nameIface = nameIOIface;
    config.creator = "EncFS " VERSION;
    config.subVersion = V5SubVersion;
    config.blockMACBytes = blockMACBytes;
    config.blockMACRandBytes = blockMACRandBytes;
    config.uniqueIV = uniqueIV;
    config.chainedNameIV = chainedIV;
    config.externalIVChaining = externalIV;

    cout << "\n";
    // xgroup(setup)
    cout << _("Configuration finished.  The filesystem to be created has\n"
	"the following properties:") << endl;
    showFSInfo( config );
    
    if( config.externalIVChaining )
    {
	cout << 
	    _("-------------------------- WARNING --------------------------\n")
	    <<
	    _("The external initialization-vector chaining option has been\n"
	      "enabled.  This option disables the use of hard links on the\n"
	      "filesystem. Without hard links, some programs may not work.\n"
	      "The programs 'mutt' and 'procmail' are known to fail.  For\n"
	      "more information, please see the encfs mailing list.\n"
	      "If you would like to choose another configuration setting,\n"
	      "please press CTRL-C now to abort and start over.") << endl;
	cout << endl;
    }

    // xgroup(setup)
    cout << _("Now you will need to enter a password for your filesystem.\n"
	"You will need to remember this password, as there is absolutely\n"
	"no recovery mechanism.  However, the password can be changed\n"
	"later using encfsctl.\n\n");

    int encodedKeySize = cipher->encodedKeySize();
    unsigned char *encodedKey = new unsigned char[ encodedKeySize ];

    CipherKey volumeKey = cipher->newRandomKey();

    // get user key and use it to encode volume key
    CipherKey userKey;
    rDebug( "useStdin: %i", useStdin );
    if(useStdin)
	userKey = getUserKey( cipher, useStdin );
    else if(passwordProgram.empty())
	userKey = getNewUserKey( cipher );
    else
	userKey = getUserKey( passwordProgram, cipher, rootDir );

    cipher->writeKey( volumeKey, encodedKey, userKey );
    userKey.reset();

    config.keyData.assign( (char*)encodedKey, encodedKeySize );
    delete[] encodedKey;

    if(volumeKey.isNull())
    {
	rWarning(_("Failure generating new volume key! "
		    "Please report this error."));
	return rootInfo;
    }

    if(!saveConfig( Config_V5, rootDir, &config ))
	return rootInfo;

    // fill in config struct
    Ptr<NameIO> nameCoder = NameIO::New( config.nameIface, 
	    cipher, volumeKey );
    if(nameCoder.isNull())
    {
	rWarning(_("Name coding interface not supported"));
	cout << _("The filename encoding interface requested is not available") 
	    << endl;
	return rootInfo;
    }
	
    nameCoder->setChainedNameIV( config.chainedNameIV );

    Ptr<DirNode::Config> dirNodeConfig (new DirNode::Config);
    dirNodeConfig->cipher = cipher;
    dirNodeConfig->key = volumeKey;
    dirNodeConfig->nameCoding = nameCoder;
    dirNodeConfig->fsSubVersion = config.subVersion;
    dirNodeConfig->blockSize = blockSize;
    dirNodeConfig->inactivityTimer = enableIdleTracking;
    dirNodeConfig->blockMACBytes = config.blockMACBytes;
    dirNodeConfig->blockMACRandBytes = config.blockMACRandBytes;
    dirNodeConfig->uniqueIV = config.uniqueIV;
    dirNodeConfig->externalIVChaining = config.externalIVChaining;
    dirNodeConfig->forceDecode = forceDecode;

    rootInfo = RootPtr( new EncFS_Root );
    rootInfo->cipher = cipher;
    rootInfo->volumeKey = volumeKey;
    rootInfo->root = Ptr<DirNode>( new DirNode( rootDir, dirNodeConfig ));

    return rootInfo;
}

void showFSInfo( const EncFSConfig &config )
{
    Ptr<Cipher> cipher = Cipher::New( config.cipherIface, -1 );
    {
	cout << autosprintf(
		// xgroup(diag)
		_("Filesystem cipher: \"%s\", version %i:%i:%i"),
		config.cipherIface.name().c_str(), config.cipherIface.current(),
		config.cipherIface.revision(), config.cipherIface.age());
	// check if we support this interface..
	if(cipher.isNull())
	    cout << _(" (NOT supported)\n");
	else
	{
	    // if we're using a newer interface, show the version number
	    if( config.cipherIface != cipher->interface() )
	    {
		Interface iface = cipher->interface();
		// xgroup(diag)
		cout << autosprintf(_(" (using %i:%i:%i)\n"),
			iface.current(), iface.revision(), iface.age());
	    } else
		cout << "\n";
	}
    }
    {
	// xgroup(diag)
	cout << autosprintf(_("Filename encoding: \"%s\", version %i:%i:%i"),
		config.nameIface.name().c_str(), config.nameIface.current(),
		config.nameIface.revision(), config.nameIface.age());
	    
	// check if we support the filename encoding interface..
	Ptr<NameIO> nameCoder = NameIO::New( config.nameIface, 
    		cipher, OpaqueValue() );
	if(nameCoder.isNull())
	{
	    // xgroup(diag)
	    cout << _(" (NOT supported)\n");
	} else
	{
	    // if we're using a newer interface, show the version number
	    if( config.nameIface != nameCoder->interface() )
	    {
		Interface iface = nameCoder->interface();
		cout << autosprintf(_(" (using %i:%i:%i)\n"),
			iface.current(), iface.revision(), iface.age());
	    } else
		cout << "\n";
	}
    }
    {
	cout << autosprintf(_("Key Size: %i bits"), config.keySize);
	cipher = Cipher::New( config.cipherIface, config.keySize );
	if(cipher.isNull())
	{
	    // xgroup(diag)
	    cout << _(" (NOT supported)\n");
	} else
	    cout << "\n";
    }
    if(config.blockMACBytes)
    {
	if(config.subVersion < 20040813)
	{
	    cout << autosprintf(
		    // xgroup(diag)
		    _("Block Size: %i bytes + %i byte MAC header"),
		    config.blockSize, 
		    config.blockMACBytes + config.blockMACRandBytes) << endl;
	} else
	{
	    // new version stores the header as part of that block size..
	    cout << autosprintf(
		    // xgroup(diag)
		    _("Block Size: %i bytes, including %i byte MAC header"),
		    config.blockSize,
		    config.blockMACBytes + config.blockMACRandBytes) << endl;
	}
    } else
    {
	// xgroup(diag)
	cout << autosprintf(_("Block Size: %i bytes"), config.blockSize);
	cout << "\n";
    }

    if(config.uniqueIV)
    {
	// xgroup(diag)
	cout << _("Each file contains 8 byte header with unique IV data.\n");
    }
    if(config.chainedNameIV)
    {
	// xgroup(diag)
	cout << _("Filenames encoded using IV chaining mode.\n");
    }
    if(config.externalIVChaining)
    {
	// xgroup(diag)
	cout << _("File data IV is chained to filename IV.\n");
    }
    cout << "\n";
}
    
CipherKey getUserKey( const Ptr<Cipher> &cipher, bool useStdin )
{
    const int MaxPassBuf = 64;
    char passBuf[MaxPassBuf];
    char *res;

    if( useStdin )
    {
	res = fgets( passBuf, sizeof(passBuf) - 1, stdin );
	// Kill the trailing newline.
	passBuf[ strlen(passBuf)-1 ] = '\0';
    } else
    {
	// xgroup(common)
	res = readpassphrase( _("EncFS Password: "),
		passBuf, sizeof(passBuf)-1, RPP_ECHO_OFF );
    }

    CipherKey userKey;
    if(!res)
	cerr << _("Zero length password not allowed\n");
    else
	userKey = cipher->newKey( passBuf, strlen(passBuf) );

    memset( passBuf, 0, sizeof(passBuf) );

    return userKey;
}

std::string readPassword( int FD )
{
    char buffer[2048];
    string result;

    while(1)
    {
	ssize_t rdSize = recv(FD, buffer, sizeof(buffer), 0);

	if(rdSize > 0)
	{
	    result.append( buffer, rdSize );
	    memset(buffer, 0, sizeof(buffer));
	} else
	    break;
    }

    // chop off trailing "\n" if present..
    // This is done so that we can use standard programs like ssh-askpass
    // without modification, as it returns trailing newline..
    if(!result.empty() && result[ result.length()-1 ] == '\n' )
	result.resize( result.length() -1 );

    return result;
}

CipherKey getUserKey( const std::string &passProg, const Ptr<Cipher> &cipher,
	const std::string &rootDir )
{
    // have a child process run the command and get the result back to us.
    int fds[2], pid;
    int res;
    CipherKey result;

    res = socketpair(PF_UNIX, SOCK_STREAM, 0, fds);
    if(res == -1)
    {
	perror(_("Internal error: socketpair() failed"));
	return result;
    }
    rDebug("getUserKey: fds = %i, %i", fds[0], fds[1]);

    pid = fork();
    if(pid == -1)
    {
	perror(_("Internal error: fork() failed"));
	close(fds[0]);
	close(fds[1]);
	return result;
    }

    if(pid == 0)
    {
	char *argv[2];
	argv[0] = const_cast<char*>(passProg.c_str());
	argv[1] = 0;

	// child process.. run the command and send output to fds[0]
	close(fds[1]); // we don't use the other half..

	// make a copy of stdout and stderr descriptors, and set an environment
	// variable telling where to find them, in case a child wants it..
	int stdOutCopy = dup( STDOUT_FILENO );
	int stdErrCopy = dup( STDERR_FILENO );
	// replace STDOUT with our socket, which we'll used to receive the
	// password..
	dup2( fds[0], STDOUT_FILENO );

	// ensure that STDOUT_FILENO and stdout/stderr are not closed on exec..
	fcntl(STDOUT_FILENO, F_SETFD, 0); // don't close on exec..
	fcntl(stdOutCopy, F_SETFD, 0);
	fcntl(stdErrCopy, F_SETFD, 0);

	char tmpBuf[8];

	setenv(ENCFS_ENV_ROOTDIR, rootDir.c_str(), 1);

	snprintf(tmpBuf, sizeof(tmpBuf)-1, "%i", stdOutCopy);
	setenv(ENCFS_ENV_STDOUT, tmpBuf, 1);
	
	snprintf(tmpBuf, sizeof(tmpBuf)-1, "%i", stdErrCopy);
	setenv(ENCFS_ENV_STDERR, tmpBuf, 1);

	execvp( argv[0], argv ); // returns only on error..

	perror(_("Internal error: failed to exec program"));
	exit(1);
    }

    close(fds[0]);
    string password = readPassword(fds[1]);
    close(fds[1]);

    waitpid(pid, NULL, 0);

    // convert to key..
    result = cipher->newKey( password.c_str(), password.length() );

    // clear buffer..
    password.assign( password.length(), '\0' );

    return result;
}

CipherKey getNewUserKey( const Ptr<Cipher> &cipher )
{
    const int MaxPassBuf = 64;
    CipherKey userKey;
    char passBuf[MaxPassBuf];
    char passBuf2[MaxPassBuf];

    do
    {
	// xgroup(common)
	char *res1 = readpassphrase(_("New Encfs Password: "), passBuf,
		sizeof(passBuf)-1, RPP_ECHO_OFF);
	// xgroup(common)
	char *res2 = readpassphrase(_("Verify Encfs Password: "), passBuf2,
		sizeof(passBuf2)-1, RPP_ECHO_OFF);

	if(res1 && res2 && !strcmp(passBuf, passBuf2))
	    userKey = cipher->newKey( passBuf, strlen(passBuf) );
	else
	{
	    // xgroup(common) -- probably not common, but group with the others
	    cerr << _("Passwords did not match, please try again\n");
	}

	memset( passBuf, 0, sizeof(passBuf) );
	memset( passBuf2, 0, sizeof(passBuf2) );
    } while( userKey.isNull() );

    return userKey;
}

RootPtr initFS( const EncFS_Opts *opts )
{
    RootPtr rootInfo;
    EncFSConfig config;

    if(readConfig( opts->rootDir, &config ) != Config_None)
    {
	// first, instanciate the cipher.
	Ptr<Cipher> cipher = Cipher::New( config.cipherIface, config.keySize );
	if(cipher.isNull())
	{
	    rError(_("Unable to find cipher %s, version %i:%i:%i"),
		    config.cipherIface.name().c_str(),
		    config.cipherIface.current(),
		    config.cipherIface.revision(),
		    config.cipherIface.age());
	    // xgroup(diag)
	    cout << _("The requested cipher interface is not available\n");
	    return rootInfo;
	}

	// get user key
	CipherKey userKey;
	rDebug( "useStdin: %i", opts->useStdin );
	if(opts->passwordProgram.empty())
	    userKey = getUserKey( cipher, opts->useStdin );
	else
	    userKey = getUserKey( opts->passwordProgram, 
		    cipher, opts->rootDir );

	if(userKey.isNull())
	    return rootInfo;

	// no funny stuff
	rDebug("configuration key size = %i", (int)config.keyData.length());
	rDebug("cipher key size = %i", cipher->encodedKeySize());
	rAssert( (int)config.keyData.length() == cipher->encodedKeySize() ); 
	// decode volume key..
	CipherKey volumeKey = cipher->readKey( 
		(unsigned char*)config.keyData.data(), userKey, opts->checkKey);
	userKey.reset();

	if(volumeKey.isNull())
	{
	    // xgroup(diag)
	    cout << _("Error decoding volume key, password incorrect\n");
	    return rootInfo;
	}

	Ptr<NameIO> nameCoder = NameIO::New( config.nameIface, 
		cipher, volumeKey );
	if(nameCoder.isNull())
	{
	    rError(_("Unable to find nameio interface %s, version %i:%i:%i"),
		    config.nameIface.name().c_str(),
		    config.nameIface.current(),
		    config.nameIface.revision(),
		    config.nameIface.age());
	    // xgroup(diag)
	    cout << _("The requested filename coding interface is "
		"not available\n");
	    return rootInfo;
	}
   
	nameCoder->setChainedNameIV( config.chainedNameIV );

	Ptr<DirNode::Config> dirNodeConfig (new DirNode::Config);
	dirNodeConfig->cipher = cipher;
	dirNodeConfig->key = volumeKey;
	dirNodeConfig->nameCoding = nameCoder;
	dirNodeConfig->fsSubVersion = config.subVersion;
	dirNodeConfig->blockSize = config.blockSize;
	dirNodeConfig->inactivityTimer = opts->idleTracking;
	dirNodeConfig->blockMACBytes = config.blockMACBytes;
	dirNodeConfig->blockMACRandBytes = config.blockMACRandBytes;
	dirNodeConfig->uniqueIV = config.uniqueIV;
	dirNodeConfig->externalIVChaining = config.externalIVChaining;
	dirNodeConfig->forceDecode = opts->forceDecode;

	rootInfo = RootPtr( new EncFS_Root );
	rootInfo->cipher = cipher;
	rootInfo->volumeKey = volumeKey;
	rootInfo->root = Ptr<DirNode>( 
		new DirNode( opts->rootDir, dirNodeConfig ));
    } else
    {
	if(opts->createIfNotFound)
	{
	    // creating a new encrypted filesystem
	    rootInfo = createV5Config( opts->rootDir, opts->idleTracking,
		    opts->forceDecode, opts->passwordProgram, opts->useStdin );
	}
    }
	
    return rootInfo;
}
