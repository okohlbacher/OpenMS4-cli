// Copyright (c) 2002-present, OpenMS Inc. -- EKU Tuebingen, ETH Zurich, and FU Berlin
// SPDX-License-Identifier: BSD-3-Clause
//
// --------------------------------------------------------------------------
// $Maintainer: Chris Bielow $
// $Authors: Chris Bielow $
// --------------------------------------------------------------------------

#pragma once

#include <OpenMS/APPLICATIONS/OpenMSCLIConfig.h>

#include <OpenMS/DATASTRUCTURES/ToolDescription.h>
#include <OpenMS/DATASTRUCTURES/StringUtils.h>
#include <OpenMS/DATASTRUCTURES/StringListUtils.h>

#include <map>


namespace OpenMS
{
  /**
    @brief Registry of independently installed tool packages and legacy internal tools.

    Packages install tab-separated manifests in share/openms4/tools. Each row contains
    tool name, category, product version, and a binary path relative to that prefix.
    OPENMS_TOOL_PREFIX_PATH adds installation prefixes (the native path-list separator).
    Duplicate names and unsafe relative paths are rejected; manifests are read on demand.
    @ingroup System
  */
  /// Map: TOPP tool name -> its @ref Internal::ToolDescription (category + per-type configuration).
  typedef std::map<std::string, Internal::ToolDescription> ToolListType;

  class OPENMSCLI_DLLAPI ToolHandler
  {
public:

    /** @brief Return installed tools merged with the legacy internal-tool registry. */
    static ToolListType getTOPPToolList();

    /** @brief Resolve a packaged tool, then sibling/PATH compatibility locations.
        @param[in] toolname Executable name without a platform extension.
        @return Absolute executable path.
        @throws Exception::FileNotFound if the tool is unavailable. */
    static std::string findExecutable(const std::string& toolname);

    /** @brief Look up the independently released product version.
        @param[in] toolname Name recorded in a package manifest.
        @return Product version, or an empty string for an unregistered tool. */
    static std::string getToolVersion(const std::string& toolname);


    /**
      @brief Return the alternative "types" / sub-commands a tool supports, or an empty list if it has none.

      Most tools have a single behaviour; a small number expose multiple sub-modes via @c -type
      (e.g. @c FeatureFinderCentroided vs. @c FeatureFinderIsotopeWavelet sharing infrastructure).

      @param[in] toolname Name of the TOPP tool to query.
      @return Type names (may be empty); empty also when the tool is unknown.
    */
    static StringList getTypes(const std::string& toolname);

    /**
      @brief Return the KNIME-style category string of a tool.

      @param[in] toolname Name of the TOPP tool to query.
      @return Category string (e.g. @c "Quantitation") or an empty string if @p toolname is unknown.
    */
    static std::string getCategory(const std::string& toolname);

    /**
      @brief Resolved file-system path of the internal-tool config directory (root of the @c .ttd search).
      @return @c File::getOpenMSDataPath() + @c "/TOOLS/INTERNAL".
    */
    static std::string getInternalToolsPath();

private:

    /// Lazily load the internal tool registry from @c .ttd files under @ref getInternalToolsPath
    static std::vector<Internal::ToolDescription> getInternalTools_();

    /// Enumerate @c .ttd config file paths under @ref getInternalToolsPath (and its OS-specific subdir)
    static StringList getInternalToolConfigFiles_();

  };

} // namespace OpenMS
