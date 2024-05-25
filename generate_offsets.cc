#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>



int main(int argc, char *argv[])
{
    int exit_code(0);

    if(argc == 2)
    {
        try
        {
            std::filesystem::path p(argv[1]);
            std::fstream ifs(p, std::fstream::in);
            if(!ifs.is_open())
                throw std::runtime_error(std::format("unable to open input file [{}]", p.string()));

            std::filesystem::path h(p.filename());
            h.replace_extension(".inc");
            std::fstream ofs(h, std::fstream::out);
            if(!ofs.is_open())
                throw std::runtime_error(std::format("unable to create output file [{}]", h.string()));

            int32_t offset_min = 0;
            int32_t offset_max = 0;

            for(std::string line; std::getline(ifs, line);)
            {
                std::istringstream iss(line);

                std::string drive;
                std::getline(iss, drive, '\t');
                std::string offset;
                std::getline(iss, offset, '\t');

                if(drive.empty() || offset.empty() || offset == "[Purged]")
                    continue;

                {
                    int32_t o = stoi(offset);
                    if(o < offset_min)
                        offset_min = o;
                    if(o > offset_max)
                        offset_max = o;
                }

                // if(drive[0] == '-')
                //     drive.insert(drive.begin(), ' ');

                ofs << std::format("{{\"{}\", {}}}, ", drive, offset) << std::endl;
            }

            std::cout << std::format("offset min: {}, offset max: {:+}", offset_min, offset_max) << std::endl;
        }
        catch(const std::string &e)
        {
            std::cout << e << std::endl;
            exit_code = 1;
        }
        catch(const std::exception &e)
        {
            std::cout << "STD exception: " << e.what() << std::endl;
            exit_code = 2;
        }
        catch(...)
        {
            std::cout << "unhandled exception" << std::endl;
            exit_code = 3;
        }
    }
    else
        std::cout << "usage: generate_offsets driveoffsets.txt" << std::endl;

    return exit_code;
}
