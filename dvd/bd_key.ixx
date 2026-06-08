module;
#include <string>

export module bd.key;

import bd.aacs;
import common;
import filesystem.udf;
import options;
import readers.disc_read_reader;
import utils.logger;



namespace gpsxre
{

export int redumper_bdkey(Context &ctx, Options &options)
{
    int exit_code = 0;

    if(ctx.disc_type != DiscType::BLURAY)
        return exit_code;

    Disc_READ_Reader reader(*ctx.sptd, 0);

    int mkbv = AACS::getMKBVersion(&reader);
    if(mkbv != -1)
    {
        LOG("BD-Video Protection: AACS (MKB version {})", mkbv);
    }

    return exit_code;
}


}
