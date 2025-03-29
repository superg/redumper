module;
#include <algorithm>
#include <cstddef>
#include <cstdint>

export module cd.generate;

import cd.cd;
import cd.cdrom;
import cd.ecc;
import cd.edc;



namespace gpsxre
{

export void regenerate_data_sector(Sector &sector, int32_t lba)
{
    std::copy_n(CD_DATA_SYNC, sizeof(CD_DATA_SYNC), sector.sync);
    sector.header.address = LBA_to_BCDMSF(lba);

    if(sector.header.mode == 1)
    {
        std::fill_n(sector.mode1.intermediate, sizeof(sector.mode1.intermediate), 0x00);

        Sector::ECC ecc = ECC().Generate((uint8_t *)&sector.header);
        std::copy_n(ecc.p_parity, sizeof(ecc.p_parity), sector.mode1.ecc.p_parity);
        std::copy_n(ecc.q_parity, sizeof(ecc.q_parity), sector.mode1.ecc.q_parity);

        sector.mode1.edc = EDC().update((uint8_t *)&sector, offsetof(Sector, mode1.edc)).final();
    }
    else if(sector.header.mode == 2)
    {
        sector.mode2.xa.sub_header_copy = sector.mode2.xa.sub_header;

        // Form2
        if(sector.mode2.xa.sub_header.submode & (uint8_t)CDXAMode::FORM2)
        {
            // can be zeroed, regenerate only if it was set
            if(sector.mode2.xa.form2.edc)
                sector.mode2.xa.form2.edc = EDC().update((uint8_t *)&sector.mode2.xa.sub_header, offsetof(Sector, mode2.xa.form2.edc) - offsetof(Sector, mode2.xa.sub_header)).final();
        }
        // Form1
        else
        {
            sector.mode2.xa.form1.edc = EDC().update((uint8_t *)&sector.mode2.xa.sub_header, offsetof(Sector, mode2.xa.form1.edc) - offsetof(Sector, mode2.xa.sub_header)).final();

            // modifies sector, make sure sector data is not used after ECC calculation, otherwise header has to be restored
            Sector::Header header = sector.header;
            std::fill_n((uint8_t *)&sector.header, sizeof(sector.header), 0x00);

            Sector::ECC ecc = ECC().Generate((uint8_t *)&sector.header);
            std::copy_n(ecc.p_parity, sizeof(ecc.p_parity), sector.mode2.xa.form1.ecc.p_parity);
            std::copy_n(ecc.q_parity, sizeof(ecc.q_parity), sector.mode2.xa.form1.ecc.q_parity);

            // restore modified sector header
            sector.header = header;
        }
    }
}

}
