//@author Erik Edwards
//@date 2019-2020


#include <iostream>
#include <fstream>
#include <unistd.h>
#include <string>
#include <cstring>
#include <valarray>
#include <complex>
#include <unordered_map>
#include <argtable2.h>
#include "/home/erik/codee/cmli/cmli.hpp"
#include "/home/erik/codee/openvoice/openvoice.h"


int main(int argc, char *argv[])
{
    using namespace std;


    //Declarations
    int ret = 0;
    const string errstr = ": \033[1;31merror:\033[0m ";
    const string warstr = ": \033[1;35mwarning:\033[0m ";
    const string progstr(__FILE__,string(__FILE__).find_last_of("/")+1,strlen(__FILE__)-string(__FILE__).find_last_of("/")-5);
    const valarray<uint8_t> oktypes = {1,2};
    const size_t I = 1, O = 1;
    ifstream ifs1; ofstream ofs1;
    int8_t stdi1, stdo1, wo1;
    ioinfo i1, o1;
    int dim, ndct, K;
    double Q;


    //Description
    string descr;
    descr += "Gets cepstral coefficients (CCs) of RxC input matrix X,\n";
    descr += "where X is usually a spectrogram.\n";
    descr += "This does 1D DCT-II along rows or cols, and then applies lifter.\n";
    descr += "\n";
    descr += "Use -d (--dim) to give the dimension along which to transform.\n";
    descr += "Use -d0 to operate along cols, and -d1 to operate along rows.\n";
    descr += "The default is 0 (along cols), unless X is a row vector.\n";
    descr += "\n";
    descr += "Use -n (--ndct) to specify transform length [default is R or C].\n";
    descr += "X is zero-padded as necessary to match ndct.\n";
    descr += "\n";
    descr += "Use -q (--Q) to specify the lifter \"bandwidth\".\n";
    descr += "The default [Q=22.0] is that of HTK, Kaldi, and D. Ellis.\n";
    descr += "Set Q to 0 to skip the lifter.\n";
    descr += "\n";
    descr += "Use -k (--K) to specify how many CCs to keep at the end.\n";
    descr += "The default is to keep all [K=ndct], but K=13 is a typical choice.\n";
    descr += "\n";
    descr += "The output (Y) is real-valued with size: \n";
    descr += "d=0 :   K x C \n";
    descr += "d=1 :   R x K \n";
    descr += "\n";
    descr += "Examples:\n";
    descr += "$ get_ccs -n256 X -o Y \n";
    descr += "$ get_ccs -n256 -d1 -k13 X > Y \n";
    descr += "$ cat X | get_ccs -n256 -k13 > Y \n";


    //Argtable
    int nerrs;
    struct arg_file  *a_fi = arg_filen(nullptr,nullptr,"<file>",I-1,I,"input file (X)");
    struct arg_int    *a_d = arg_intn("d","dim","<uint>",0,1,"dimension along which to take DCT [default=0]");
    struct arg_int    *a_n = arg_intn("n","ndct","<uint>",0,1,"transform length [default is R or C]");
    struct arg_int    *a_k = arg_intn("k","K","<uint>",0,1,"num CCs to keep [default=ndct]");
    struct arg_dbl    *a_q = arg_dbln("q","Q","<dbl>",0,1,"lifter bandwidth parameter [default=22.0]");
    struct arg_file  *a_fo = arg_filen("o","ofile","<file>",0,O,"output file (Y)");
    struct arg_lit *a_help = arg_litn("h","help",0,1,"display this help and exit");
    struct arg_end  *a_end = arg_end(5);
    void *argtable[] = {a_fi, a_d, a_n, a_k, a_q, a_fo, a_help, a_end};
    if (arg_nullcheck(argtable)!=0) { cerr << progstr+": " << __LINE__ << errstr << "problem allocating argtable" << endl; return 1; }
    nerrs = arg_parse(argc, argv, argtable);
    if (a_help->count>0)
    {
        cout << "Usage: " << progstr; arg_print_syntax(stdout, argtable, "\n");
        cout << endl; arg_print_glossary(stdout, argtable, "  %-25s %s\n");
        cout << endl << descr; return 1;
    }
    if (nerrs>0) { arg_print_errors(stderr,a_end,(progstr+": "+to_string(__LINE__)+errstr).c_str()); return 1; }


    //Check stdin
    stdi1 = (a_fi->count==0 || strlen(a_fi->filename[0])==0 || strcmp(a_fi->filename[0],"-")==0);
    if (stdi1>0 && isatty(fileno(stdin))) { cerr << progstr+": " << __LINE__ << errstr << "no stdin detected" << endl; return 1; }


    //Check stdout
    if (a_fo->count>0) { stdo1 = (strlen(a_fo->filename[0])==0 || strcmp(a_fo->filename[0],"-")==0); }
    else { stdo1 = (!isatty(fileno(stdout))); }
    wo1 = (stdo1 || a_fo->count>0);


    //Open input
    if (stdi1) { ifs1.copyfmt(cin); ifs1.basic_ios<char>::rdbuf(cin.rdbuf()); } else { ifs1.open(a_fi->filename[0]); }
    if (!ifs1) { cerr << progstr+": " << __LINE__ << errstr << "problem opening input file" << endl; return 1; }


    //Read input header
    if (!read_input_header(ifs1,i1)) { cerr << progstr+": " << __LINE__ << errstr << "problem reading header for input file" << endl; return 1; }
    if ((i1.T==oktypes).sum()==0)
    {
        cerr << progstr+": " << __LINE__ << errstr << "input data type must be in " << "{";
        for (auto o : oktypes) { cerr << int(o) << ((o==oktypes[oktypes.size()-1]) ? "}" : ","); }
        cerr << endl; return 1;
    }


    //Get options

    //Get dim
    if (a_d->count==0) { dim = 0; }
    else if (a_d->ival[0]<0) { cerr << progstr+": " << __LINE__ << errstr << "dim must be nonnegative" << endl; return 1; }
    else { dim = a_d->ival[0]; }
    if (dim!=0 && dim!=1) { cerr << progstr+": " << __LINE__ << errstr << "dim must be 0 or 1" << endl; return 1; }

    //Get ndct
    if (a_n->count==0) { ndct = (dim==0) ? int(i1.R) : int(i1.C); }
    else if (a_n->ival[0]<1) { cerr << progstr+": " << __LINE__ << errstr << "ndct must be positive" << endl; return 1; }
    else { ndct = a_n->ival[0]; }

    //Get K
    if (a_k->count==0) { K = ndct; }
    else if (a_k->ival[0]<1) { cerr << progstr+": " << __LINE__ << errstr << "K must be positive" << endl; return 1; }
    else if (a_k->ival[0]>ndct) { cerr << progstr+": " << __LINE__ << errstr << "K must be <= ndct" << endl; return 1; }
    else { K = a_k->ival[0]; }

    //Get Q
    Q = (a_q->count>0) ? a_q->dval[0] : 22.0;
    if (Q<=0.0) { cerr << progstr+": " << __LINE__ << errstr << "Q must be positive" << endl; return 1; }


    //Checks
    if (!i1.ismat()) { cerr << progstr+": " << __LINE__ << errstr << "input (X) must be 1D or 2D" << endl; return 1; }
    if (i1.isempty()) { cerr << progstr+": " << __LINE__ << errstr << "input (X) found to be empty" << endl; return 1; }
    if (dim==0 && ndct<int(i1.R)) { cerr << progstr+": " << __LINE__ << errstr << "ndct must be > nrows of X" << endl; return 1; }
    if (dim==1 && ndct<int(i1.C)) { cerr << progstr+": " << __LINE__ << errstr << "ndct must be > ncols of X" << endl; return 1; }
    if (K>ndct) { cerr << progstr+": " << __LINE__ << errstr << "K must be <= ndct" << endl; return 1; }


    //Set output header info
    o1.F = i1.F; o1.T = i1.T;
    o1.R = (dim==0) ? uint32_t(K) : i1.R;
    o1.C = (dim==1) ? uint32_t(K) : i1.C;
    o1.S = i1.S; o1.H = i1.H;


    //Open output
    if (wo1)
    {
        if (stdo1) { ofs1.copyfmt(cout); ofs1.basic_ios<char>::rdbuf(cout.rdbuf()); } else { ofs1.open(a_fo->filename[0]); }
        if (!ofs1) { cerr << progstr+": " << __LINE__ << errstr << "problem opening output file 1" << endl; return 1; }
    }


    //Write output header
    if (wo1 && !write_output_header(ofs1,o1)) { cerr << progstr+": " << __LINE__ << errstr << "problem writing header for output file 1" << endl; return 1; }


    //Other prep


    //Process
    if (i1.T==1)
    {
        float *X, *Y;
        try { X = new float[i1.N()]; }
        catch (...) { cerr << progstr+": " << __LINE__ << errstr << "problem allocating for input file 1 (X)" << endl; return 1; }
        try { Y = new float[o1.N()]; }
        catch (...) { cerr << progstr+": " << __LINE__ << errstr << "problem allocating for output file (Y)" << endl; return 1; }
        try { ifs1.read(reinterpret_cast<char*>(X),i1.nbytes()); }
        catch (...) { cerr << progstr+": " << __LINE__ << errstr << "problem reading input file 1 (X)" << endl; return 1; }
        if (ov::get_ccs_s(Y,X,i1.iscolmajor(),int(i1.R),int(i1.C),dim,ndct,float(Q),K)) { cerr << progstr+": " << __LINE__ << errstr << "problem during function call" << endl; return 1; }
        if (wo1)
        {
            try { ofs1.write(reinterpret_cast<char*>(Y),o1.nbytes()); }
            catch (...) { cerr << progstr+": " << __LINE__ << errstr << "problem writing output file (Y)" << endl; return 1; }
        }
        delete[] X; delete[] Y;
    }
    else if (i1.T==2)
    {
        double *X, *Y;
        try { X = new double[i1.N()]; }
        catch (...) { cerr << progstr+": " << __LINE__ << errstr << "problem allocating for input file 1 (X)" << endl; return 1; }
        try { Y = new double[o1.N()]; }
        catch (...) { cerr << progstr+": " << __LINE__ << errstr << "problem allocating for output file (Y)" << endl; return 1; }
        try { ifs1.read(reinterpret_cast<char*>(X),i1.nbytes()); }
        catch (...) { cerr << progstr+": " << __LINE__ << errstr << "problem reading input file 1 (X)" << endl; return 1; }
        if (ov::get_ccs_d(Y,X,i1.iscolmajor(),int(i1.R),int(i1.C),dim,ndct,double(Q),K)) { cerr << progstr+": " << __LINE__ << errstr << "problem during function call" << endl; return 1; }
        if (wo1)
        {
            try { ofs1.write(reinterpret_cast<char*>(Y),o1.nbytes()); }
            catch (...) { cerr << progstr+": " << __LINE__ << errstr << "problem writing output file (Y)" << endl; return 1; }
        }
        delete[] X; delete[] Y;
    }
    else
    {
        cerr << progstr+": " << __LINE__ << errstr << "data type not supported" << endl; return 1;
    }
    

    //Exit
    return ret;
}
