// SPDX-License-Identifier: BSD-3-Clause
#include <OpenMS/ANALYSIS/MAPMATCHING/TransformationModelDefaults.h>
#include <OpenMS/APPLICATIONS/MapAlignerBase.h>
#include <OpenMS/CONCEPT/ClassTest.h>
using namespace OpenMS;
START_TEST(MapAlignerDefaults, "$Id$")
START_SECTION((CLI model defaults preserve scientific defaults))
for (const std::string selected : {"linear", "b_spline", "lowess", "interpolated", "none", "custom"})
{
  TEST_TRUE(TransformationModelDefaults::getDefaults(selected) == MapAlignerBase::getModelDefaults(selected))
}
END_SECTION
END_TEST
