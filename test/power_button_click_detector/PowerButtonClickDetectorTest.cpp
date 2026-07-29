#include <gtest/gtest.h>

#include "src/util/PowerButtonClickDetector.h"

TEST(PowerButtonClickDetectorTest, ResolvesSingleImmediatelyWhenMultiClickIsDisabled) {
  PowerButtonClickDetector detector;

  detector.update(true, false, false, 10);

  EXPECT_EQ(detector.finalizedClicks(), 1);
  EXPECT_FALSE(detector.hasPendingClicks());
}

TEST(PowerButtonClickDetectorTest, WaitsForTheMultiClickWindowBeforeResolvingSingle) {
  PowerButtonClickDetector detector;

  detector.update(true, false, true, 10);
  detector.update(false, false, true, 409);
  EXPECT_EQ(detector.finalizedClicks(), 0);

  detector.update(false, false, true, 410);
  EXPECT_EQ(detector.finalizedClicks(), 1);
}

TEST(PowerButtonClickDetectorTest, ResolvesDoubleAfterTheSecondClickWindow) {
  PowerButtonClickDetector detector;

  detector.update(true, false, true, 10);
  detector.update(true, false, true, 200);
  detector.update(false, false, true, 600);

  EXPECT_EQ(detector.finalizedClicks(), 2);
}

TEST(PowerButtonClickDetectorTest, ResolvesTripleImmediately) {
  PowerButtonClickDetector detector;

  detector.update(true, false, true, 10);
  detector.update(true, false, true, 100);
  detector.update(true, false, true, 200);

  EXPECT_EQ(detector.finalizedClicks(), 3);
  EXPECT_FALSE(detector.hasPendingClicks());
}

TEST(PowerButtonClickDetectorTest, DoesNotFinalizeWhileTheNextClickIsHeld) {
  PowerButtonClickDetector detector;

  detector.update(true, false, true, 10);
  detector.update(false, true, true, 500);
  EXPECT_EQ(detector.finalizedClicks(), 0);

  detector.update(true, false, true, 550);
  detector.update(false, false, true, 950);
  EXPECT_EQ(detector.finalizedClicks(), 2);
}

TEST(PowerButtonClickDetectorTest, CancelDropsPendingAndFinalizedClicks) {
  PowerButtonClickDetector detector;

  detector.update(true, false, true, 10);
  detector.cancel();
  detector.update(false, false, true, 500);

  EXPECT_EQ(detector.finalizedClicks(), 0);
  EXPECT_FALSE(detector.hasPendingClicks());
}
