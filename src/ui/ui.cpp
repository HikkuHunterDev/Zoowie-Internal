#include "ui/ui.h"

#include "ui/overlay.h"

#include <string_view>

#ifdef OPENCHANGER_DEV
#include "core/log.h"
#include "game/js.h"

#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <string>
#endif

namespace oc::ui {

namespace {

#ifdef OPENCHANGER_DEV

std::optional<std::string> readFile(const char *Path) {
  std::ifstream File(Path, std::ios::binary);
  if (!File) {
    OC_LOG("ui", "{} not found", Path);
    return std::nullopt;
  }
  return std::string(std::istreambuf_iterator<char>(File), {});
}

void log(const std::string &Text) { OC_LOG("js", "{}", Text); }

JsMethod<&log> Log{"dev", "log", "text"};

#else

constexpr char Script[] = {
#embed OC_UI_BUNDLE_JS suffix(, )
    0};
constexpr char Css[] = {
#embed OC_UI_BUNDLE_CSS suffix(, )
    0};

#endif

} // namespace

bool start() {
  // Overlay UI disabled when controlled by external Zoowie menu.
  return true;
}

void stop() { }

} // namespace oc::ui
