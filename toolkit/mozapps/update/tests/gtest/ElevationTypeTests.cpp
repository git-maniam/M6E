/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "elevation_type.h"


class ElevationTypeTest : public ::testing::Test {
  void SetUp() override {
  }

  void TearDown() override {}
};

TEST_F(ElevationTypeTest, IsProcessElevated_NotElevatedTest) {
  BoolResult res = isProcessElevated();
  EXPECT_EQ(res, BoolResult::False);
}

TEST_F(ElevationTypeTest, IsProcessElevated_NonElevatedCmd) {
  BoolResult res = isProcessElevated("hello there");
  EXPECT_EQ(res, BoolResult::False);
}

TEST_F(ElevationTypeTest, IsProcessElevated_ElevatedCmd) {
  BoolResult res =
      isProcessElevated("/Library/PrivilegedHelperTools/org.mozilla.updater");
  EXPECT_EQ(res, BoolResult::True);
}
