/*
 * Copyright 2011, Ben Langmead <langmea@cs.jhu.edu>
 *
 * This file is part of Bowtie 2.
 *
 * Bowtie 2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Bowtie 2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Bowtie 2.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <fstream>
#include <string>
#include <cassert>
#include <getopt.h>
#include "assert_helpers.h"
#include "endian_swap.h"
#include "bt2_idx.h"
#include "bt2_io.h"
#include "bt2_util.h"
#include "formats.h"
#include "sequence_io.h"
#include "tokenize.h"
#include "timer.h"
#include "ref_read.h"
#include "filebuf.h"
#include "reference.h"
#include "ds.h"

/**
 * \file Driver for the bowtie-build indexing tool.
 */

// Build parameters
int verbose;
static int sanityCheck;
static int format;
static TIndexOffU bmax;
static TIndexOffU bmaxMultSqrt;
static uint32_t bmaxDivN;
static int dcv;
static int noDc;
static int entireSA;
static int seed;
static int showVersion;
static int nthreads;      // number of pthreads operating concurrently
//   Ebwt parameters
static int32_t lineRate;
static int32_t linesPerSide;
static int32_t offRate;
static int32_t ftabChars;
static int32_t localOffRate;
static int32_t localFtabChars;
static string conversion_table_fname; // conversion table file name
static string taxonomy_fname; // taxonomy tree file name
static string name_table_fname; // name table file name
static string size_table_fname; // contig size table file name
static int  bigEndian;
static bool nsToAs;
static bool doSaFile;  // make a file with just the suffix array in it
static bool doBwtFile; // make a file with just the BWT string in it
static bool autoMem;
static bool packed;
static bool writeRef;
static bool justRef;
static bool reverseEach;
static string wrapper;
static int kmer_count;

static void resetOptions() {
	verbose        = true;  // be talkative (default)
	sanityCheck    = 0;     // do slow sanity checks
	format         = FASTA; // input sequence format
	bmax           = OFF_MASK; // max blockwise SA bucket size
	bmaxMultSqrt   = OFF_MASK; // same, as multplier of sqrt(n)
	bmaxDivN       = 4;          // same, as divisor of n
	dcv            = 1024;  // bwise SA difference-cover sample sz
	noDc           = 0;     // disable difference-cover sample
	entireSA       = 0;     // 1 = disable blockwise SA
	seed           = 0;     // srandom seed
	showVersion    = 0;     // just print version and quit?
    nthreads       = 1;
	//   Ebwt parameters
	lineRate       = Ebwt<TIndexOffU>::default_lineRate;
	linesPerSide   = 1;  // 1 64-byte line on a side
	offRate        = 4;  // sample 1 out of 16 SA elts
	ftabChars      = 10; // 10 chars in initial lookup table
    localOffRate   = 3;
    localFtabChars = 6;
    conversion_table_fname    = "";
    taxonomy_fname = "";
    name_table_fname = "";
    size_table_fname = "";
	bigEndian      = 0;  // little endian
	nsToAs         = false; // convert reference Ns to As prior to indexing
    doSaFile       = false; // make a file with just the suffix array in it
    doBwtFile      = false; // make a file with just the BWT string in it
	autoMem        = true;  // automatically adjust memory usage parameters
	packed         = false; //
	writeRef       = true;  // write compact reference to .3.bt2/.4.bt2
	justRef        = false; // *just* write compact reference, don't index
	reverseEach    = false;
    wrapper.clear();
    kmer_count     = 0; // k : k-mer to be counted
}

// Argument constants for getopts
enum {
	ARG_BMAX = 256,
	ARG_BMAX_MULT,
	ARG_BMAX_DIV,
	ARG_DCV,
	ARG_SEED,
	ARG_CUTOFF,
	ARG_PMAP,
	ARG_NTOA,
	ARG_USAGE,
	ARG_REVERSE_EACH,
    ARG_SA,
	ARG_WRAPPER,
    ARG_THREADS,
    ARG_LOCAL_OFFRATE,
    ARG_LOCAL_FTABCHARS,
    ARG_CONVERSION_TABLE,
    ARG_TAXONOMY_TREE,
    ARG_NAME_TABLE,
    ARG_SIZE_TABLE,
    ARG_KMER_COUNT,
};

/**
 * Print a detailed usage message to the provided output stream.
 */
static void printUsage(ostream& out) {
	out << "Centrifuge version " << string(CENTRIFUGE_VERSION).c_str() << " by Daehwan Kim (infphilo@gmail.com, http://www.ccb.jhu.edu/people/infphilo)" << endl;
	string tool_name = "centrifuge-build-bin";
	if(wrapper == "basic-0") {
		tool_name = "centrifuge-build";
	}
    
	out << "Usage: centrifuge-build [options]* --conversion-table <table file> --taxonomy-tree <taxonomy tree file> <reference_in> <cf_index_base>" << endl
	    << "    reference_in            comma-separated list of files with ref sequences" << endl
	    << "    centrifuge_index_base          write " << gEbwt_ext << " data to files with this dir/basename" << endl
        << "Options:" << endl
        << "    -c                      reference sequences given on cmd line (as" << endl
        << "                            <reference_in>)" << endl;
    if(wrapper == "basic-0") {
        //out << "    --large-index           force generated index to be 'large', even if ref" << endl
	//	<< "                            has fewer than 4 billion nucleotides" << endl;
	}
    out << "    -a/--noauto             disable automatic -p/--bmax/--dcv memory-fitting" << endl
	    << "    --bmax <int>            max bucket sz for blockwise suffix-array builder" << endl
	    << "    --bmaxdivn <int>        max bucket sz as divisor of ref len (default: 4)" << endl
	    << "    --dcv <int>             diff-cover period for blockwise (default: 1024)" << endl
	    << "    --nodc                  disable diff-cover (algorithm becomes quadratic)" << endl
	    << "    -r/--noref              don't build .3/.4.bt2 (packed reference) portion" << endl
	    << "    -3/--justref            just build .3/.4.bt2 (packed reference) portion" << endl
	    << "    -o/--offrate <int>      SA is sampled every 2^offRate BWT chars (default: 5)" << endl
	    << "    -t/--ftabchars <int>    # of chars consumed in initial lookup (default: 10)" << endl
        << "    --conversion-table <file name>  a table that converts any id to a taxonomy id" << endl
        << "    --taxonomy-tree    <file name>  taxonomy tree" << endl
        << "    --name-table       <file name>  names corresponding to taxonomic IDs" << endl
        << "    --size-table       <file name>  table of contig (or genome) sizes" << endl
	    << "    --seed <int>            seed for random number generator" << endl
	    << "    -q/--quiet              verbose output (for debugging)" << endl
        << "    -p/--threads <int>      number of alignment threads to launch (1)" << endl
        << "    --kmer-count <int>      k size for counting the number of distinct k-mer" << endl
	    << "    -h/--help               print detailed description of tool and its options" << endl
	    << "    --usage                 print this usage message" << endl
	    << "    --version               print version information and quit" << endl
	    ;
    
    if(wrapper.empty()) {
		cerr << endl
        << "*** Warning ***" << endl
        << "'" << tool_name << "' was run directly.  It is recommended "
        << "that you run the wrapper script 'bowtie2-build' instead."
        << endl << endl;
	}
}

static const char *short_options = "qrap:h?nscfl:i:o:t:h:3C";

static struct option long_options[] = {
	{(char*)"quiet",          no_argument,       0,            'q'},
	{(char*)"sanity",         no_argument,       0,            's'},
	{(char*)"packed",         no_argument,       0,            'p'},
	{(char*)"little",         no_argument,       &bigEndian,   0},
	{(char*)"big",            no_argument,       &bigEndian,   1},
	{(char*)"bmax",           required_argument, 0,            ARG_BMAX},
	{(char*)"bmaxmultsqrt",   required_argument, 0,            ARG_BMAX_MULT},
	{(char*)"bmaxdivn",       required_argument, 0,            ARG_BMAX_DIV},
	{(char*)"dcv",            required_argument, 0,            ARG_DCV},
	{(char*)"nodc",           no_argument,       &noDc,        1},
	{(char*)"seed",           required_argument, 0,            ARG_SEED},
	{(char*)"entiresa",       no_argument,       &entireSA,    1},
	{(char*)"version",        no_argument,       &showVersion, 1},
	{(char*)"noauto",         no_argument,       0,            'a'},
	{(char*)"noblocks",       required_argument, 0,            'n'},
    {(char*)"threads",        required_argument, 0,            ARG_THREADS},
	{(char*)"linerate",       required_argument, 0,            'l'},
	{(char*)"linesperside",   required_argument, 0,            'i'},
	{(char*)"offrate",        required_argument, 0,            'o'},
	{(char*)"ftabchars",      required_argument, 0,            't'},
    {(char*)"localoffrate",   required_argument, 0,            ARG_LOCAL_OFFRATE},
	{(char*)"localftabchars", required_argument, 0,            ARG_LOCAL_FTABCHARS},
    {(char*)"conversion-table", required_argument, 0,          ARG_CONVERSION_TABLE},
    {(char*)"taxonomy-tree",    required_argument, 0,          ARG_TAXONOMY_TREE},
    {(char*)"name-table",       required_argument, 0,          ARG_NAME_TABLE},
    {(char*)"size-table",       required_argument, 0,          ARG_SIZE_TABLE},
	{(char*)"help",           no_argument,       0,            'h'},
	{(char*)"ntoa",           no_argument,       0,            ARG_NTOA},
	{(char*)"justref",        no_argument,       0,            '3'},
	{(char*)"noref",          no_argument,       0,            'r'},
	{(char*)"kmer-count",     required_argument, 0,            ARG_KMER_COUNT},
    {(char*)"sa",             no_argument,       0,            ARG_SA},
	{(char*)"reverse-each",   no_argument,       0,            ARG_REVERSE_EACH},
	{(char*)"usage",          no_argument,       0,            ARG_USAGE},
    {(char*)"wrapper",        required_argument, 0,            ARG_WRAPPER},
	{(char*)0, 0, 0, 0} // terminator
};

/**
 * Parse an int out of optarg and enforce that it be at least 'lower';
 * if it is less than 'lower', then output the given error message and
 * exit with an error and a usage message.
 */
template<typename T>
static T parseNumber(T lower, const char *errmsg) {
	char *endPtr= NULL;
	T t = (T)strtoll(optarg, &endPtr, 10);
	if (endPtr != NULL) {
		if (t < lower) {
			cerr << errmsg << endl;
			printUsage(cerr);
			throw 1;
		}
		return t;
	}
	cerr << errmsg << endl;
	printUsage(cerr);
	throw 1;
	return -1;
}

/**
 * Read command-line arguments
 */
static void parseOptions(int argc, const char **argv) {
	int option_index = 0;
	int next_option;
	do {
		next_option = getopt_long(
			argc, const_cast<char**>(argv),
			short_options, long_options, &option_index);
		switch (next_option) {
            case ARG_WRAPPER:
				wrapper = optarg;
				break;
			case 'f': format = FASTA; break;
			case 'c': format = CMDLINE; break;
            case ARG_THREADS:
			case 'p': nthreads = parseNumber<int>(1, "-p arg must be at least 1");
                break;
			case 'C':
				cerr << "Error: -C specified but Bowtie 2 does not support colorspace input." << endl;
				throw 1;
				break;
			case 'l':
				lineRate = parseNumber<int>(3, "-l/--lineRate arg must be at least 3");
				break;
			case 'i':
				linesPerSide = parseNumber<int>(1, "-i/--linesPerSide arg must be at least 1");
				break;
			case 'o':
				offRate = parseNumber<int>(0, "-o/--offRate arg must be at least 0");
				break;
            case ARG_LOCAL_OFFRATE:
                localOffRate = parseNumber<int>(0, "-o/--localoffrate arg must be at least 0");
                break;
			case '3':
				justRef = true;
				break;
			case 't':
				ftabChars = parseNumber<int>(1, "-t/--ftabChars arg must be at least 1");
				break;
            case ARG_LOCAL_FTABCHARS:
				localFtabChars = parseNumber<int>(1, "-t/--localftabchars arg must be at least 1");
				break;
			case 'n':
				// all f-s is used to mean "not set", so put 'e' on end
				bmax = 0xfffffffe;
				break;
			case 'h':
			case ARG_USAGE:
				printUsage(cout);
				throw 0;
				break;
			case ARG_BMAX:
				bmax = parseNumber<TIndexOffU>(1, "--bmax arg must be at least 1");
				bmaxMultSqrt = OFF_MASK; // don't use multSqrt
				bmaxDivN = 0xffffffff;     // don't use multSqrt
				break;
			case ARG_BMAX_MULT:
				bmaxMultSqrt = parseNumber<TIndexOffU>(1, "--bmaxmultsqrt arg must be at least 1");
				bmax = OFF_MASK;     // don't use bmax
				bmaxDivN = 0xffffffff; // don't use multSqrt
				break;
			case ARG_BMAX_DIV:
				bmaxDivN = parseNumber<uint32_t>(1, "--bmaxdivn arg must be at least 1");
				bmax = OFF_MASK;         // don't use bmax
				bmaxMultSqrt = OFF_MASK; // don't use multSqrt
				break;
			case ARG_DCV:
				dcv = parseNumber<int>(3, "--dcv arg must be at least 3");
				break;
			case ARG_SEED:
				seed = parseNumber<int>(0, "--seed arg must be at least 0");
				break;
			case ARG_REVERSE_EACH:
				reverseEach = true;
				break;
            case ARG_SA:
                doSaFile = true;
                break;
			case ARG_NTOA: nsToAs = true; break;
            case ARG_CONVERSION_TABLE:
                conversion_table_fname = optarg;
                break;
            case ARG_TAXONOMY_TREE:
                taxonomy_fname = optarg;
                break;
            case ARG_NAME_TABLE:
                name_table_fname = optarg;
                break;
            case ARG_SIZE_TABLE:
                size_table_fname = optarg;
                break;
            case ARG_KMER_COUNT:
                kmer_count = parseNumber<int>(1, "--kmer-count arg must be at least 1");
                break;
			case 'a': autoMem = false; break;
			case 'q': verbose = false; break;
			case 's': sanityCheck = true; break;
			case 'r': writeRef = false; break;

			case -1: /* Done with options. */
				break;
			case 0:
				if (long_options[option_index].flag != 0)
					break;
			default:
				printUsage(cerr);
				throw 1;
		}
	} while(next_option != -1);
	if(bmax < 40) {
		cerr << "Warning: specified bmax is very small (" << bmax << ").  This can lead to" << endl
		     << "extremely slow performance and memory exhaustion.  Perhaps you meant to specify" << endl
		     << "a small --bmaxdivn?" << endl;
	}
}

EList<string> filesWritten;

/**
 * Delete all the index files that we tried to create.  For when we had to
 * abort the index-building process due to an error.
 */
static void deleteIdxFiles(
                           const string& outfile,
                           bool doRef,
                           bool justRef)
{
	
	for(size_t i = 0; i < filesWritten.size(); i++) {
		cerr << "Deleting \"" << filesWritten[i].c_str()
		     << "\" file written during aborted indexing attempt." << endl;
		remove(filesWritten[i].c_str());
	}
}

extern void initializeCntLut();

/**
 * Drive the index construction process and optionally sanity-check the
 * result.
 */
template<typename TStr>
static void driver(
                   const string& infile,
                   EList<string>& infiles,
                   const string& conversion_table_fname,
                   const string& taxonomy_fname,
                   const string& name_table_fname,
                   const string& size_table_fname,
                   const string& outfile,
                   bool packed,
                   int reverse)
{
    initializeCntLut();
	EList<FileBuf*> is(MISC_CAT);
	bool bisulfite = false;
	RefReadInParams refparams(false, reverse, nsToAs, bisulfite);
	assert_gt(infiles.size(), 0);
	if(format == CMDLINE) {
		// Adapt sequence strings to stringstreams open for input
		stringstream *ss = new stringstream();
		for(size_t i = 0; i < infiles.size(); i++) {
			(*ss) << ">" << i << endl << infiles[i].c_str() << endl;
		}
		FileBuf *fb = new FileBuf(ss);
		assert(fb != NULL);
		assert(!fb->eof());
		assert(fb->get() == '>');
		ASSERT_ONLY(fb->reset());
		assert(!fb->eof());
		is.push_back(fb);
	} else {
		// Adapt sequence files to ifstreams
		for(size_t i = 0; i < infiles.size(); i++) {
			FILE *f = fopen(infiles[i].c_str(), "r");
			if (f == NULL) {
				cerr << "Error: could not open "<< infiles[i].c_str() << endl;
				throw 1;
			}
			FileBuf *fb = new FileBuf(f);
			assert(fb != NULL);
			if(fb->peek() == -1 || fb->eof()) {
				cerr << "Warning: Empty fasta file: '" << infile.c_str() << "'" << endl;
				continue;
			}
			assert(!fb->eof());
			assert(fb->get() == '>');
			ASSERT_ONLY(fb->reset());
			assert(!fb->eof());
			is.push_back(fb);
		}
	}
	if(is.empty()) {
		cerr << "Warning: All fasta inputs were empty" << endl;
		throw 1;
	}
	// Vector for the ordered list of "records" comprising the input
	// sequences.  A record represents a stretch of unambiguous
	// characters in one of the input sequences.
	EList<RefRecord> szs(MISC_CAT);
	std::pair<size_t, size_t> sztot;
	{
		if(verbose) cout << "Reading reference sizes" << endl;
		Timer _t(cout, "  Time reading reference sizes: ", verbose);
        sztot = BitPairReference::szsFromFasta(is, string(), bigEndian, refparams, szs, sanityCheck);
	}
	if(justRef) return;
	assert_gt(sztot.first, 0);
	assert_gt(sztot.second, 0);
	assert_gt(szs.size(), 0);
	// Construct index from input strings and parameters
	filesWritten.push_back(outfile + ".1." + gEbwt_ext);
	filesWritten.push_back(outfile + ".2." + gEbwt_ext);
    filesWritten.push_back(outfile + ".3." + gEbwt_ext);
	TStr s;
	Ebwt<TIndexOffU> ebwt(
                          s,
                          packed,
                          0,
                          1,  // TODO: maybe not?
                          lineRate,
                          offRate,      // suffix-array sampling rate
                          ftabChars,    // number of chars in initial arrow-pair calc
                          outfile,      // basename for .?.ebwt files
                          reverse == 0, // fw
                          !entireSA,    // useBlockwise
                          bmax,         // block size for blockwise SA builder
                          bmaxMultSqrt, // block size as multiplier of sqrt(len)
                          bmaxDivN,     // block size as divisor of len
                          noDc? 0 : dcv,// difference-cover period
                          nthreads,
                          is,           // list of input streams
                          szs,          // list of reference sizes
                          (TIndexOffU)sztot.first,  // total size of all unambiguous ref chars
                          conversion_table_fname,
                          taxonomy_fname,
                          name_table_fname,
                          size_table_fname,
                          refparams,    // reference read-in parameters
                          seed,         // pseudo-random number generator seed
                          -1,           // override offRate
                          doSaFile,     // make a file with just the suffix array in it
                          doBwtFile,    // make a file with just the BWT string in it
                          kmer_count,   // Count the number of distinct k-mers if non-zero
                          verbose,      // be talkative
                          autoMem,      // pass exceptions up to the toplevel so that we can adjust memory settings automatically
                          sanityCheck); // verify results and internal consistency
	// Note that the Ebwt is *not* resident in memory at this time.  To
	// load it into memory, call ebwt.loadIntoMemory()
	if(verbose) {
		// Print Ebwt's vital stats
		ebwt.eh().print(cout);
	}
	if(sanityCheck) {
		// Try restoring the original string (if there were
		// multiple texts, what we'll get back is the joined,
		// padded string, not a list)
		ebwt.loadIntoMemory(
                            0,
                            reverse ? (refparams.reverse == REF_READ_REVERSE) : 0,
                            true,  // load SA sample?
                            true,  // load ftab?
                            true,  // load rstarts?
                            false,
                            false);
		SString<char> s2;
		ebwt.restore(s2);
		ebwt.evictFromMemory();
		{
			SString<char> joinedss = Ebwt<TIndexOffU>::join<SString<char> >(
                                                                            is,          // list of input streams
                                                                            szs,         // list of reference sizes
                                                                            (TIndexOffU)sztot.first, // total size of all unambiguous ref chars
                                                                            refparams,   // reference read-in parameters
                                                                            seed);       // pseudo-random number generator seed
			if(refparams.reverse == REF_READ_REVERSE) {
				joinedss.reverse();
			}
			assert_eq(joinedss.length(), s2.length());
			assert(sstr_eq(joinedss, s2));
		}
		if(verbose) {
			if(s2.length() < 1000) {
				cout << "Passed restore check: " << s2.toZBuf() << endl;
			} else {
				cout << "Passed restore check: (" << s2.length() << " chars)" << endl;
			}
		}
	}
}

static const char *argv0 = NULL;

extern "C" {
/**
 * main function.  Parses command-line arguments.
 */
int centrifuge_build(int argc, const char **argv) {
	string outfile;
	try {
		// Reset all global state, including getopt state
		opterr = optind = 1;
		resetOptions();

		string infile;
		EList<string> infiles(MISC_CAT);

		parseOptions(argc, argv);
		argv0 = argv[0];
		if(showVersion) {
			cout << argv0 << " version " << string(CENTRIFUGE_VERSION).c_str() << endl;
			if(sizeof(void*) == 4) {
				cout << "32-bit" << endl;
			} else if(sizeof(void*) == 8) {
				cout << "64-bit" << endl;
			} else {
				cout << "Neither 32- nor 64-bit: sizeof(void*) = " << sizeof(void*) << endl;
			}
			cout << "Built on " << BUILD_HOST << endl;
			cout << BUILD_TIME << endl;
			cout << "Compiler: " << COMPILER_VERSION << endl;
			cout << "Options: " << COMPILER_OPTIONS << endl;
			cout << "Sizeof {int, long, long long, void*, size_t, off_t}: {"
				 << sizeof(int)
				 << ", " << sizeof(long) << ", " << sizeof(long long)
				 << ", " << sizeof(void *) << ", " << sizeof(size_t)
				 << ", " << sizeof(off_t) << "}" << endl;
			return 0;
		}

		// Get input filename
		if(optind >= argc) {
			cerr << "No input sequence or sequence file specified!" << endl;
			printUsage(cerr);
			return 1;
		}
		infile = argv[optind++];

		// Get output filename
		if(optind >= argc) {
			cerr << "No output file specified!" << endl;
			printUsage(cerr);
			return 1;
		}
		outfile = argv[optind++];

		tokenize(infile, ",", infiles);
		if(infiles.size() < 1) {
			cerr << "Tokenized input file list was empty!" << endl;
			printUsage(cerr);
			return 1;
		}
        
        if(conversion_table_fname == "") {
            cerr << "Please specify --conversion-table!" << endl;
            printUsage(cerr);
            return 1;
        }
        
        if(taxonomy_fname == "") {
            cerr << "Please specify --taxonomy-tree!" << endl;
            printUsage(cerr);
            return 1;
        }
        
        if(name_table_fname == "") {
            cerr << "Please specify --name-table!" << endl;
            printUsage(cerr);
            return 1;
        }

		// Optionally summarize
		if(verbose) {
			cout << "Settings:" << endl
				 << "  Output files: \"" << outfile.c_str() << ".*." << gEbwt_ext << "\"" << endl
				 << "  Line rate: " << lineRate << " (line is " << (1<<lineRate) << " bytes)" << endl
				 << "  Lines per side: " << linesPerSide << " (side is " << ((1<<lineRate)*linesPerSide) << " bytes)" << endl
				 << "  Offset rate: " << offRate << " (one in " << (1<<offRate) << ")" << endl
				 << "  FTable chars: " << ftabChars << endl
				 << "  Strings: " << (packed? "packed" : "unpacked") << endl
                 << "  Local offset rate: " << localOffRate << " (one in " << (1<<localOffRate) << ")" << endl
                 << "  Local fTable chars: " << localFtabChars << endl
				 ;
			if(bmax == OFF_MASK) {
				cout << "  Max bucket size: default" << endl;
			} else {
				cout << "  Max bucket size: " << bmax << endl;
			}
			if(bmaxMultSqrt == OFF_MASK) {
				cout << "  Max bucket size, sqrt multiplier: default" << endl;
			} else {
				cout << "  Max bucket size, sqrt multiplier: " << bmaxMultSqrt << endl;
			}
			if(bmaxDivN == 0xffffffff) {
				cout << "  Max bucket size, len divisor: default" << endl;
			} else {
				cout << "  Max bucket size, len divisor: " << bmaxDivN << endl;
			}
			cout << "  Difference-cover sample period: " << dcv << endl;
			cout << "  Endianness: " << (bigEndian? "big":"little") << endl
				 << "  Actual local endianness: " << (currentlyBigEndian()? "big":"little") << endl
				 << "  Sanity checking: " << (sanityCheck? "enabled":"disabled") << endl;
	#ifdef NDEBUG
			cout << "  Assertions: disabled" << endl;
	#else
			cout << "  Assertions: enabled" << endl;
	#endif
			cout << "  Random seed: " << seed << endl;
			cout << "  Sizeofs: void*:" << sizeof(void*) << ", int:" << sizeof(int) << ", long:" << sizeof(long) << ", size_t:" << sizeof(size_t) << endl;
			cout << "Input files DNA, " << file_format_names[format].c_str() << ":" << endl;
			for(size_t i = 0; i < infiles.size(); i++) {
				cout << "  " << infiles[i].c_str() << endl;
			}
		}
		// Seed random number generator
		srand(seed);
		{
			Timer timer(cout, "Total time for call to driver() for forward index: ", verbose);
			if(!packed) {
				try {
					driver<SString<char> >(
                                           infile,
                                           infiles,
                                           conversion_table_fname,
                                           taxonomy_fname,
                                           size_table_fname, // but def expects name_table_fname
                                           name_table_fname, // but def expects size_table_fname
                                           outfile,
                                           false,
                                           REF_READ_FORWARD);
				} catch(bad_alloc& e) {
					if(autoMem) {
						cerr << "Switching to a packed string representation." << endl;
						packed = true;
					} else {
						throw e;
					}
				}
			}
			if(packed) {
				driver<S2bDnaString>(
                                     infile,
                                     infiles,
                                     conversion_table_fname,
                                     taxonomy_fname,
                                     size_table_fname, // but def expects name_table_fname
                                     name_table_fname, // but def expects size_table_fname									 
                                     outfile,
                                     true,
                                     REF_READ_FORWARD);
			}
		}
#if 0
		int reverseType = reverseEach ? REF_READ_REVERSE_EACH : REF_READ_REVERSE;
		srand(seed);
		Timer timer(cout, "Total time for backward call to driver() for mirror index: ", verbose);
		if(!packed) {
			try {
				driver<SString<char> >(infile, infiles, outfile + ".rev", false, reverseType);
			} catch(bad_alloc& e) {
				if(autoMem) {
					cerr << "Switching to a packed string representation." << endl;
					packed = true;
				} else {
					throw e;
				}
			}
		}
		if(packed) {
			driver<S2bDnaString>(infile, infiles, outfile + ".rev", true, reverseType);
		}
#endif
		return 0;
	} catch(std::exception& e) {
		cerr << "Error: Encountered exception: '" << e.what() << "'" << endl;
		cerr << "Command: ";
		for(int i = 0; i < argc; i++) cerr << argv[i] << " ";
		cerr << endl;
		deleteIdxFiles(outfile, writeRef || justRef, justRef);
		return 1;
	} catch(int e) {
		if(e != 0) {
			cerr << "Error: Encountered internal Centrifuge exception (#" << e << ")" << endl;
			cerr << "Command: ";
			for(int i = 0; i < argc; i++) cerr << argv[i] << " ";
			cerr << endl;
		}
		deleteIdxFiles(outfile, writeRef || justRef, justRef);
		return e;
	}
}
}
