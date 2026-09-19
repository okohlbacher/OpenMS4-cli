// Copyright (c) 2002-present, OpenMS Inc. -- EKU Tuebingen, ETH Zurich, and FU Berlin
// SPDX-License-Identifier: BSD-3-Clause
//
// --------------------------------------------------------------------------
// $Maintainer: Chris Bielow $
// $Authors: Chris Bielow $
// --------------------------------------------------------------------------

#include <OpenMS/APPLICATIONS/ToolHandler.h>
#include <OpenMS/CONCEPT/Exception.h>
#include <OpenMS/FORMAT/ToolDescriptionFile.h>
#include <OpenMS/SYSTEM/File.h>
#include <OpenMS/SYSTEM/PathUtils.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#ifdef _WIN32
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

namespace
{
namespace fs = std::filesystem;
struct PackageTool
{
  std::string category;
  std::string version;
  fs::path executable;
};
void manifestAnchor()
{
}

std::vector<fs::path> prefixes()
{
  std::vector<fs::path> result;
#ifdef _WIN32
  constexpr char separator = ';';
#else
  constexpr char separator = ':';
#endif
  if (const char* value = std::getenv("OPENMS_TOOL_PREFIX_PATH"))
  {
    std::stringstream paths(value);
    for (std::string item; std::getline(paths, item, separator);)
    {
      if (! item.empty()) { result.emplace_back(item); }
    }
  }
  const fs::path executable_directory = OpenMS::to_path(OpenMS::File::getExecutablePath()) / ".";
  result.emplace_back((executable_directory / "..").lexically_normal());
#ifdef __APPLE__
  // A desktop bundle executable lives at <prefix>/bin/App.app/Contents/MacOS.
  // Its package manifest is under <prefix>/share, outside the app bundle.
  const fs::path bundle = (executable_directory / "../..").lexically_normal();
  fs::path bundle_name = bundle;
  if (bundle_name.filename().empty()) { bundle_name = bundle_name.parent_path(); }
  if (bundle_name.extension() == ".app") { result.emplace_back((executable_directory / "../../../..").lexically_normal()); }
#endif
#ifdef _WIN32
  HMODULE module = nullptr;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(&manifestAnchor), &module))
  {
    std::wstring path(32768, L'\0');
    const DWORD count = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (count > 0 && count < path.size())
    {
      path.resize(count);
      result.push_back(fs::path(path).parent_path().parent_path());
    }
  }
#else
  Dl_info info {};
  if (dladdr(reinterpret_cast<void*>(&manifestAnchor), &info) && info.dli_fname)
  {
    result.push_back(fs::path(info.dli_fname).parent_path().parent_path());
  }
#endif
  return result;
}

std::map<std::string, PackageTool> packageTools() try
{
  std::map<std::string, PackageTool> result;
  std::set<fs::path> seen;
  for (const auto& prefix : prefixes())
  {
    const auto directory = prefix / "share/openms4/tools";
    std::error_code ec;
    const bool exists = fs::is_directory(directory, ec);
    if (ec && ec != std::errc::no_such_file_or_directory)
    {
      throw fs::filesystem_error("Cannot inspect tool manifest directory", directory, ec);
    }
    if (!exists) { continue; }
    for (const auto& entry : fs::directory_iterator(directory))
    {
      if (entry.path().extension() != ".tsv" || ! entry.is_regular_file()) { continue; }
      if (! seen.insert(fs::weakly_canonical(entry.path())).second) { continue; }
      std::ifstream input(entry.path());
      if (!input)
      {
        throw OpenMS::Exception::FileNotReadable(__FILE__, __LINE__, OPENMS_PRETTY_FUNCTION, entry.path().string());
      }
      for (std::string line; std::getline(input, line);)
      {
        if (! line.empty() && line.back() == '\r') { line.pop_back(); }
        if (line.empty() || line[0] == '#') { continue; }
        std::stringstream fields(line);
        std::vector<std::string> values;
        for (std::string value; std::getline(fields, value, '\t');)
        {
          values.push_back(value);
        }
        const auto fail = [&]() {
          throw OpenMS::Exception::InvalidValue(__FILE__, __LINE__, OPENMS_PRETTY_FUNCTION, "Invalid or duplicate tool package manifest entry",
                                                entry.path().string() + ": " + line);
        };
        if (std::count(line.begin(), line.end(), '\t') != 3 || values.size() != 4 || values[0].empty() || values[2].empty() || values[3].empty())
        {
          fail();
        }
        const fs::path relative(values[3]);
        if (relative.has_root_path() || values[0].find_first_of("/\\") != std::string::npos) { fail(); }
        for (const auto& component : relative)
        {
          if (component == "..") { fail(); }
        }
        if (! result.emplace(values[0], PackageTool {values[1], values[2], fs::absolute(prefix / relative)}).second) { fail(); }
      }
      if (input.bad())
      {
        throw OpenMS::Exception::FileNotReadable(__FILE__, __LINE__, OPENMS_PRETTY_FUNCTION, entry.path().string());
      }
    }
  }
  return result;
}
catch (const fs::filesystem_error& error)
{
  throw OpenMS::Exception::InvalidValue(__FILE__, __LINE__, OPENMS_PRETTY_FUNCTION,
                                        "Cannot read tool package registry", error.what());
}
} // namespace

namespace OpenMS
{
ToolListType ToolHandler::getTOPPToolList()
{
  ToolListType tools_map;
  for (const auto& [name, entry] : packageTools())
  {
    if (entry.category != "DesktopViewer" && entry.category != "DesktopWorkflow")
    {
      tools_map.emplace(name, Internal::ToolDescription(name, entry.category));
    }
  }

  // INTERNAL tools
  // this operation is expensive, as we need to parse configuration files (*.ttd)
  std::vector<Internal::ToolDescription> internal_tools = getInternalTools_();
  for (std::vector<Internal::ToolDescription>::const_iterator it = internal_tools.begin(); it != internal_tools.end(); ++it)
  {
    if (! tools_map.contains(it->name)) { tools_map[it->name] = *it; }
    else
    {
      throw Exception::InvalidValue(__FILE__, __LINE__, OPENMS_PRETTY_FUNCTION, "Duplicate tool name error: Trying to add internal tool '" + it->name,
                                    it->name);
    }
  }

  return tools_map;
}

std::string ToolHandler::getToolVersion(const std::string& toolname)
{
  const auto tools = packageTools();
  const auto it = tools.find(toolname);
  return it == tools.end() ? std::string() : it->second.version;
}

std::string ToolHandler::findExecutable(const std::string& toolname) try
{
  const auto tools = packageTools();
  const auto it = tools.find(toolname);
  if (it != tools.end())
  {
    if (fs::is_regular_file(it->second.executable) && File::executable(it->second.executable.string())) { return it->second.executable.string(); }
    throw Exception::FileNotFound(__FILE__, __LINE__, OPENMS_PRETTY_FUNCTION, it->second.executable.string());
  }
  std::string candidate = (std::filesystem::path(File::getExecutablePath()) / toolname).string();
#ifdef _WIN32
  if (! StringUtils::hasSuffix(candidate, ".exe")) { candidate += ".exe"; }
#endif
  if (fs::is_regular_file(candidate) && File::executable(candidate)) { return candidate; }
  candidate = toolname;
  if (File::findExecutable(candidate) && fs::is_regular_file(candidate) && File::executable(candidate)) { return fs::absolute(candidate).string(); }
  throw Exception::FileNotFound(__FILE__, __LINE__, OPENMS_PRETTY_FUNCTION, toolname);
}
catch (const fs::filesystem_error& error)
{
  throw Exception::FileNotFound(__FILE__, __LINE__, OPENMS_PRETTY_FUNCTION, error.what());
}

StringList ToolHandler::getTypes(const std::string& toolname)
{
  Internal::ToolDescription ret;
  ToolListType tools = getTOPPToolList();
  if (tools.contains(toolname)) { return tools[toolname].types; }
  return {};
}

std::vector<Internal::ToolDescription> ToolHandler::getInternalTools_()
{
  // Successful first discovery is cached; failed initialization can be retried.
  static const std::vector<Internal::ToolDescription> tools = [] {
    std::vector<Internal::ToolDescription> result;
    for (const auto& file : getInternalToolConfigFiles_())
    {
      ToolDescriptionFile reader;
      std::vector<Internal::ToolDescription> entries;
      reader.load(file, entries);
      result.insert(result.end(), entries.begin(), entries.end());
    }
    return result;
  }();
  return tools;
}

std::string ToolHandler::getInternalToolsPath()
{ return File::getOpenMSDataPath() + "/TOOLS/INTERNAL"; }

StringList ToolHandler::getInternalToolConfigFiles_()
{
  StringList paths;
  // *.ttd default path
  paths.push_back(getInternalToolsPath());
  // OS-specific path
#ifdef OPENMS_WINDOWSPLATFORM
  paths.push_back(getInternalToolsPath() + "/WINDOWS");
#else
  paths.push_back(getInternalToolsPath() + "/LINUX");
#endif
  // additional environment
  if (getenv("OPENMS_TTD_INTERNAL_PATH") != nullptr) { paths.push_back(std::string(getenv("OPENMS_TTD_INTERNAL_PATH"))); }

  StringList all_files;
  for (const auto& p : paths)
  {
    StringList files;
    File::fileList(p, "*.ttd", files, true);
    all_files.insert(all_files.end(), files.begin(), files.end());
  }
  return all_files;
}

std::string ToolHandler::getCategory(const std::string& toolname)
{
  ToolListType tools = getTOPPToolList();
  std::string s;
  if (tools.contains(toolname)) { s = tools[toolname].category; }

  return s;
}

} // namespace OpenMS
