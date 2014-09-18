//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2014 Joerg Henrichs
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#include "config/hardware_stats.hpp"

#include "config/user_config.hpp"
#include "graphics/glwrap.hpp"
#include "graphics/irr_driver.hpp"
#include "online/http_request.hpp"
#include "utils/string_utils.hpp"

#ifdef __APPLE__
#  include <sys/sysctl.h>
#endif

namespace HardwareStats
{


// ----------------------------------------------------------------------------
/** Returns the amount of RAM in MB.
 *  (C) 2014 by Wildfire Games (0 A.D.), ported by Joerg Henrichs
 */
int getRAM()
{
#ifdef __linux__
    const uint64_t memory_size = (uint64_t)sysconf(_SC_PHYS_PAGES) 
                                        * sysconf(_SC_PAGESIZE);
    return int(memory_size / (1024*1024));
#endif

#ifdef WIN32
    MEMORYSTATUSEX mse;
    mse.dwLength = sizeof(mse);
    const bool ok = GlobalMemoryStatusEx(&mse)==TRUE;

    DWORDLONG memory_size = mse.ullTotalPhys;
    // Richter, "Programming Applications for Windows": the reported
    // value doesn't include non-paged pool reserved during boot;
    // it's not considered available to the kernel. (the amount is
    // 528 KiB on a 512 MiB WinXP/Win2k machine). we'll round up
    // to the nearest megabyte to fix this.
    const DWORDLONG mbyte = 1024*1024;
    return (int)ceil(memory_size/mbyte);
#endif

#ifdef __APPLE__
    size_t memory_size = 0;
    size_t len = sizeof(memory_size);
    // Argh, the API doesn't seem to be const-correct
    /*const*/ int mib[2] = { CTL_HW, HW_PHYSMEM };
    sysctl(mib, 2, &memory_size, &len, 0, 0);
    memory_size /= (1024*1024);
    return int(memory_size);
#endif
    return 0;
}   // getRAM

// ----------------------------------------------------------------------------

/** If the configuration of this installation has not been reported for the
 *  current version, collect the hardware statistics and send it to STK's
 *  server.
 */
void reportHardwareStats()
{
    // Version of the hw report, which is stored in the DB. If new fields
    // are added, increase this version. Each STK installation will report
    // its configuration only once (per version number). So if the version
    // number is increased, a new report will be sent.
    const int report_version = 1;
    if(UserConfigParams::m_last_hw_report_version>=report_version) return;

    Json json;
#ifdef WIN32
    json.add("os_win", 1);
#else
    json.add("os_win", 0);
#endif
#ifdef __APPLE__
    json.add("os_macosx", 1);
#else
    json.add("os_macosx", 0);
#endif
#ifdef __linux__
    json.add("os_linux", 1);
    json.add("os_unix", 1);
#else
    json.add("os_linux", 0);
    json.add("os_unix", 0);
#endif
#ifdef DEBUG
    json.add("build_debug", 1);
#endif

    unsigned int ogl_version = irr_driver->getGLSLVersion();
    unsigned int major = ogl_version/100;
    unsigned int minor = ogl_version - 100*major;
    std::string version = 
        StringUtils::insertValues("%d.%d", major, minor);
    json.add("GL_SHADING_LANGUAGE_VERSION", version);

    std::string vendor, renderer, full_version;
    irr_driver->getOpenGLData(&vendor, &renderer, &full_version);
    json.add("GL_VENDOR",   vendor          );
    json.add("GL_RENDERER", renderer        );
    json.add("GL_VERSION",  full_version    );
    json.add("gfx_drv_ver", "OpenGL "+vendor);

    std::string card_name = vendor;
    if(StringUtils::startsWith(card_name, "ATI Technologies Inc."))
        card_name="ATI";
    else if (StringUtils::startsWith(card_name, "NVIDIA Corporation"))
        card_name="NVIDIA";
    else if(StringUtils::startsWith(card_name, "S3 Graphics"))
        card_name="S3";
    json.add("gfx_card", card_name+" "+renderer);
    
    json.add("video_xres", UserConfigParams::m_width );
    json.add("video_yres", UserConfigParams::m_height);

    int mem = getRAM();
    if(mem>0)
        json.add("ram_total", mem);

    // Too long for debugging atm
    //json.add("GL_EXTENSIONS", getGLExtensions());
    getGLLimits(&json);
    json.finish();
    Log::verbose("json", "'%s'", json.toString().c_str());

    // ------------------------------------------------------------------------
    /** A small class which sends the HW report to the STK server. On
     *  completion, it will either update the last-submitted-hw-report version,
     *  or log an error message (in which case next time STK is started it
     *  wil try again to log the report).
     */
    class HWReportRequest : public Online::HTTPRequest
    {
    private:
        /** Version number of the hw report. */
        int m_version;
    public:
        HWReportRequest(int version) : Online::HTTPRequest(/*manage memory*/true, 1)
                                       ,m_version(version)
        {}
        // --------------------------------------------------------------------
        /** Callback after the request has been executed. 
         */
        virtual void callback()
        {
            if(hadDownloadError() || getData()=="<h1>Bad Request (400)</h1>")
            {
                Log::error("HW report", "Error uploading the HW report.");
                if(hadDownloadError())
                    Log::error("HW report", "%s", getDownloadErrorMessage());
                else
                    Log::error("HW report", "%s", getData().c_str());
            }
            else
            {
                Log::info("HW report", "Upload successful.");
                UserConfigParams::m_last_hw_report_version = m_version;
            }
        }   // callback

    };   // HWReportRequest
    // ------------------------------------------------------------------------

    Online::HTTPRequest *request = new HWReportRequest(report_version);
    request->addParameter("user_id", 3);
    request->addParameter("time", StkTime::getTimeSinceEpoch());
    request->addParameter("type", "hwdetect");
    request->addParameter("version", report_version);
    request->addParameter("data", json.toString());
    request->setURL("http://stats.supertuxkart.net/upload/v1/");
    //request->setURL("http://127.0.0.1:8000/upload/v1/");
    // FIXME: For now: don't submit
    request->queue();

}   // reportHardwareStats

}   // namespace HardwareStats
