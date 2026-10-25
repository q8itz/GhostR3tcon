#include "generated/default/graw_init.h"

#include "graw_app.h"

#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/string/utf8.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>

REXCVAR_DEFINE_BOOL(boot_to_campaign, true, "GRAW",
                    "Start Yeti_Final_Xenon.exe (Single Player) directly instead of the "
                    "default.xex shell menu");

void GrawApp::OnPreSetup(rex::RuntimeConfig&) {
  // The campaign simulates at 30 Hz. At the default 60 Hz, squad logic runs
  // ahead of the frames and a few minutes in it reads a null object.
  if (rex::cvar::GetFlagSource("video_mode_refresh_rate") == rex::cvar::Source::kDefault) {
    rex::cvar::SetFlagByName("video_mode_refresh_rate", "30");
    REXLOG_INFO("Guest display set to 30 Hz for the campaign");
  }
}

void GrawApp::OnPostLoadXexImage() {
  if (!REXCVAR_GET(boot_to_campaign)) {
    return;
  }
  rex::Runtime* rt = runtime();
  auto shell = rt->kernel_state()->GetExecutableModule();
  if (!shell) {
    return;
  }
  // Same swap the shell's Single Player entry performs through
  // XamLoaderLaunchTitle. The campaign reads no launch data from the shell.
  std::string campaign = rex::string::utf8_join_guest_paths(
      rex::string::utf8_find_base_guest_path(shell->path()), "Yeti_Final_Xenon.exe");
  if (!rt->kernel_state()->FindRecompiledModule(campaign)) {
    REXLOG_ERROR("No recompiled module for {}; starting the shell menu", campaign);
    return;
  }
  REXLOG_INFO("Booting Single Player directly: {}", campaign);
  // The shell image is released before the campaign loads, so a failure here
  // leaves no executable and the launch reports it.
  if (XFAILED(rt->ReplaceExecutable(campaign))) {
    REXLOG_ERROR("Direct campaign boot failed for {}", campaign);
  }
}
