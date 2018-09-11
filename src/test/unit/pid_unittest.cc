/*
 * This file is part of Cleanflight.
 *
 * Cleanflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Cleanflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cleanflight.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <cmath>

#include "unittest_macros.h"
#include "gtest/gtest.h"
#include "build/debug.h"

bool simulatedAirmodeEnabled = true;
float simulatedSetpointRate[3] = { 0,0,0 };
float simulatedRcDeflection[3] = { 0,0,0 };
float simulatedThrottlePIDAttenuation = 1.0f;
float simulatedMotorMixRange = 0.0f;

int16_t debug[DEBUG16_VALUE_COUNT];
uint8_t debugMode;

extern "C" {
    #include "build/debug.h"
    #include "common/axis.h"
    #include "common/maths.h"
    #include "common/filter.h"

    #include "config/config_reset.h"
    #include "pg/pg.h"
    #include "pg/pg_ids.h"

    #include "drivers/sound_beeper.h"
    #include "drivers/time.h"

    #include "fc/core.h"
    #include "fc/rc.h"

    #include "fc/rc_controls.h"
    #include "fc/runtime_config.h"

    #include "flight/pid.h"
    #include "flight/imu.h"
    #include "flight/mixer.h"

    #include "io/gps.h"

    #include "sensors/gyro.h"
    #include "sensors/acceleration.h"

    gyro_t gyro;
    attitudeEulerAngles_t attitude;

    float getThrottlePIDAttenuation(void) { return simulatedThrottlePIDAttenuation; }
    float getMotorMixRange(void) { return simulatedMotorMixRange; }
    float getSetpointRate(int axis) { return simulatedSetpointRate[axis]; }
    bool isAirmodeActivated() { return simulatedAirmodeEnabled; }
    float getRcDeflectionAbs(int axis) { return ABS(simulatedRcDeflection[axis]); }
    void systemBeep(bool) { }
    bool gyroOverflowDetected(void) { return false; }
    float getRcDeflection(int axis) { return simulatedRcDeflection[axis]; }
    void beeperConfirmationBeeps(uint8_t) { }
}

pidProfile_t *pidProfile;
rollAndPitchTrims_t rollAndPitchTrims = { { 0, 0 } };

int loopIter = 0;

// Always use same defaults for testing in future releases even when defaults change
void setDefaultTestSettings(void) {
    pgResetAll();
    pidProfile = pidProfilesMutable(1);
    pidProfile->pid[PID_ROLL]  =  { 40, 40, 30, 65 };
    pidProfile->pid[PID_PITCH] =  { 58, 50, 35, 60 };
    pidProfile->pid[PID_YAW]   =  { 70, 45, 20, 60 };
    pidProfile->pid[PID_LEVEL] =  { 50, 50, 75, 0 };

    pidProfile->pidSumLimit = PIDSUM_LIMIT;
    pidProfile->pidSumLimitYaw = PIDSUM_LIMIT_YAW;
    pidProfile->yaw_lowpass_hz = 0;
    pidProfile->dterm_lowpass_hz = 100;
    pidProfile->dterm_lowpass2_hz = 0;
    pidProfile->dterm_notch_hz = 260;
    pidProfile->dterm_notch_cutoff = 160;
    pidProfile->dterm_filter_type = FILTER_BIQUAD;
    pidProfile->itermWindupPointPercent = 50;
    pidProfile->vbatPidCompensation = 0;
    pidProfile->pidAtMinThrottle = PID_STABILISATION_ON;
    pidProfile->levelAngleLimit = 55;
    pidProfile->feedForwardTransition = 100;
    pidProfile->yawRateAccelLimit = 100;
    pidProfile->rateAccelLimit = 0;
    pidProfile->antiGravityMode = ANTI_GRAVITY_SMOOTH;
    pidProfile->itermThrottleThreshold = 250;
    pidProfile->itermAcceleratorGain = 1000;
    pidProfile->crash_time = 500;
    pidProfile->crash_delay = 0;
    pidProfile->crash_recovery_angle = 10;
    pidProfile->crash_recovery_rate = 100;
    pidProfile->crash_dthreshold = 50;
    pidProfile->crash_gthreshold = 400;
    pidProfile->crash_setpoint_threshold = 350;
    pidProfile->crash_recovery = PID_CRASH_RECOVERY_OFF;
    pidProfile->horizon_tilt_effect = 75;
    pidProfile->horizon_tilt_expert_mode = false;
    pidProfile->crash_limit_yaw = 200;
    pidProfile->itermLimit = 150;
    pidProfile->throttle_boost = 0;
    pidProfile->throttle_boost_cutoff = 15;
    pidProfile->iterm_rotation = false;
    pidProfile->smart_feedforward = false,
    pidProfile->iterm_relax = ITERM_RELAX_OFF,
    pidProfile->iterm_relax_cutoff = 11,
    pidProfile->iterm_relax_type = ITERM_RELAX_SETPOINT,
    pidProfile->abs_control_gain = 0,

    gyro.targetLooptime = 4000;
}

timeUs_t currentTestTime(void) {
    return targetPidLooptime * loopIter++;
}

void resetTest(void) {
    loopIter = 0;
    simulatedThrottlePIDAttenuation = 1.0f;
    simulatedMotorMixRange = 0.0f;

    pidStabilisationState(PID_STABILISATION_OFF);
    DISABLE_ARMING_FLAG(ARMED);

    setDefaultTestSettings();
    for (int axis = FD_ROLL; axis <= FD_YAW; axis++) {
        pidData[axis].P = 0;
        pidData[axis].I = 0;
        pidData[axis].D = 0;
        pidData[axis].F = 0;
        pidData[axis].Sum = 0;
        simulatedSetpointRate[axis] = 0;
        simulatedRcDeflection[axis] = 0;
        gyro.gyroADCf[axis] = 0;
    }
    attitude.values.roll = 0;
    attitude.values.pitch = 0;
    attitude.values.yaw = 0;

    flightModeFlags = 0;
    pidInit(pidProfile);

    // Run pidloop for a while after reset
    for (int loop = 0; loop < 20; loop++) {
        pidController(pidProfile, &rollAndPitchTrims, currentTestTime());
    }
}

void setStickPosition(int axis, float stickRatio) {
    simulatedSetpointRate[axis] = 1998.0f * stickRatio;
    simulatedRcDeflection[axis] = stickRatio;
}

// All calculations will have 10% tolerance
float calculateTolerance(float input) {
    return fabs(input * 0.1f);
}

TEST(pidControllerTest, testInitialisation)
{
    resetTest();

    // In initial state PIDsums should be 0
    for (int axis = 0; axis <= FD_YAW; axis++) {
        EXPECT_FLOAT_EQ(0, pidData[axis].P);
        EXPECT_FLOAT_EQ(0, pidData[axis].I);
        EXPECT_FLOAT_EQ(0, pidData[axis].D);
    }
}

TEST(pidControllerTest, testStabilisationDisabled) {
    ENABLE_ARMING_FLAG(ARMED);
    // Run few loops to make sure there is no error building up when stabilisation disabled

    for (int loop = 0; loop < 10; loop++) {
        pidController(pidProfile, &rollAndPitchTrims, currentTestTime());

        // PID controller should not do anything, while stabilisation disabled
        EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].P);
        EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].P);
        EXPECT_FLOAT_EQ(0, pidData[FD_YAW].P);
        EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].I);
        EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].I);
        EXPECT_FLOAT_EQ(0, pidData[FD_YAW].I);
        EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].D);
        EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].D);
        EXPECT_FLOAT_EQ(0, pidData[FD_YAW].D);
    }
}

TEST(pidControllerTest, testPidLoop) {
    // Make sure to start with fresh values
    resetTest();
    ENABLE_ARMING_FLAG(ARMED);
    pidStabilisationState(PID_STABILISATION_ON);

    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());

    // Loop 1 - Expecting zero since there is no error
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].D);

    // Add some rotation on ROLL to generate error
    gyro.gyroADCf[FD_ROLL] = 100;
    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());

    // Loop 2 - Expect PID loop reaction to ROLL error
    ASSERT_NEAR(-128.1, pidData[FD_ROLL].P, calculateTolerance(-128.1));
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].P);
    ASSERT_NEAR(-7.8, pidData[FD_ROLL].I, calculateTolerance(-7.8));
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].I);
    ASSERT_NEAR(-198.4, pidData[FD_ROLL].D, calculateTolerance(-198.4));
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].D);

    // Add some rotation on PITCH to generate error
    gyro.gyroADCf[FD_PITCH] = -100;
    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());

    // Loop 3 - Expect PID loop reaction to PITCH error, ROLL is still in error
    ASSERT_NEAR(-128.1, pidData[FD_ROLL].P, calculateTolerance(-128.1));
    ASSERT_NEAR(185.8, pidData[FD_PITCH].P, calculateTolerance(185.8));
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].P);
    ASSERT_NEAR(-15.6, pidData[FD_ROLL].I, calculateTolerance(-15.6));
    ASSERT_NEAR(9.8, pidData[FD_PITCH].I, calculateTolerance(9.8));
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].D);
    ASSERT_NEAR(231.4, pidData[FD_PITCH].D, calculateTolerance(231.4));
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].D);

    // Add some rotation on YAW to generate error
    gyro.gyroADCf[FD_YAW] = 100;
    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());

    // Loop 4 - Expect PID loop reaction to PITCH error, ROLL and PITCH are still in error
    ASSERT_NEAR(-128.1, pidData[FD_ROLL].P, calculateTolerance(-128.1));
    ASSERT_NEAR(185.8, pidData[FD_PITCH].P, calculateTolerance(185.8));
    ASSERT_NEAR(-224.2, pidData[FD_YAW].P, calculateTolerance(-224.2));
    ASSERT_NEAR(-23.5, pidData[FD_ROLL].I, calculateTolerance(-23.5));
    ASSERT_NEAR(19.6, pidData[FD_PITCH].I, calculateTolerance(19.6));
    ASSERT_NEAR(-8.7, pidData[FD_YAW].I, calculateTolerance(-8.7));
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].D);
    ASSERT_NEAR(-132.25, pidData[FD_YAW].D, calculateTolerance(-132.25));

    // Simulate Iterm behaviour during mixer saturation
    simulatedMotorMixRange = 1.2f;
    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());
    ASSERT_NEAR(-23.5, pidData[FD_ROLL].I, calculateTolerance(-23.5));
    ASSERT_NEAR(19.6, pidData[FD_PITCH].I, calculateTolerance(19.6));
    ASSERT_NEAR(-8.8, pidData[FD_YAW].I, calculateTolerance(-8.8));
    simulatedMotorMixRange = 0;

    // Match the stick to gyro to stop error
    simulatedSetpointRate[FD_ROLL] = 100;
    simulatedSetpointRate[FD_PITCH] = -100;
    simulatedSetpointRate[FD_YAW] = 100;

    for(int loop = 0; loop < 5; loop++) {
        pidController(pidProfile, &rollAndPitchTrims, currentTestTime());
    }
    // Iterm is stalled as it is not accumulating anymore
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].P);
    ASSERT_NEAR(-23.5, pidData[FD_ROLL].I, calculateTolerance(-23.5));
    ASSERT_NEAR(19.6, pidData[FD_PITCH].I, calculateTolerance(19.6));
    ASSERT_NEAR(-10.6, pidData[FD_YAW].I, calculateTolerance(-10.6));
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].D);

    // Now disable Stabilisation
    pidStabilisationState(PID_STABILISATION_OFF);
    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());

    // Should all be zero again
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].D);
}

TEST(pidControllerTest, testPidLevel) {
    // Make sure to start with fresh values
    resetTest();
    ENABLE_ARMING_FLAG(ARMED);
    pidStabilisationState(PID_STABILISATION_ON);

    // Test Angle mode response
    enableFlightMode(ANGLE_MODE);
    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());

    // Loop 1
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].D);

    // Test attitude response
    setStickPosition(FD_ROLL, 1.0f);
    setStickPosition(FD_PITCH, -1.0f);
    attitude.values.roll = 550;
    attitude.values.pitch = -550;
    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());
    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());
    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());

    // Loop 2
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].D);

    // Disable ANGLE_MODE on full stick inputs
    disableFlightMode(ANGLE_MODE);
    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());

    // Expect full rate output
    ASSERT_NEAR(2559.8, pidData[FD_ROLL].P, calculateTolerance(2559.8));
    ASSERT_NEAR(-3711.6, pidData[FD_PITCH].P, calculateTolerance(-3711.6));
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].P);
    ASSERT_NEAR(150, pidData[FD_ROLL].I, calculateTolerance(150));
    ASSERT_NEAR(-150, pidData[FD_PITCH].I, calculateTolerance(-150));
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].D);
}


TEST(pidControllerTest, testPidHorizon) {
    resetTest();
    ENABLE_ARMING_FLAG(ARMED);
    pidStabilisationState(PID_STABILISATION_ON);
    enableFlightMode(HORIZON_MODE);

    // Loop 1
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].D);

    // Test full stick response
    setStickPosition(FD_ROLL, 1.0f);
    setStickPosition(FD_PITCH, -1.0f);
    attitude.values.roll = 550;
    attitude.values.pitch = -550;
    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());

    // Expect full rate output on full stick
    ASSERT_NEAR(2559.8, pidData[FD_ROLL].P, calculateTolerance(2559.8));
    ASSERT_NEAR(-3711.6, pidData[FD_PITCH].P, calculateTolerance(-3711.6));
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].P);
    ASSERT_NEAR(150, pidData[FD_ROLL].I, calculateTolerance(150));
    ASSERT_NEAR(-150, pidData[FD_PITCH].I, calculateTolerance(-150));
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].D);

    // Test full stick response
    setStickPosition(FD_ROLL, 0.1f);
    setStickPosition(FD_PITCH, -0.1f);
    attitude.values.roll = 536;
    attitude.values.pitch = -536;
    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());

    ASSERT_NEAR(0.75, pidData[FD_ROLL].P, calculateTolerance(0.75));
    ASSERT_NEAR(-1.09, pidData[FD_PITCH].P, calculateTolerance(-1.09));
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].P);
    ASSERT_NEAR(150, pidData[FD_ROLL].I, calculateTolerance(150));
    ASSERT_NEAR(-150, pidData[FD_PITCH].I, calculateTolerance(-150));
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].D);
}

TEST(pidControllerTest, testMixerSaturation) {
    resetTest();
    ENABLE_ARMING_FLAG(ARMED);
    pidStabilisationState(PID_STABILISATION_ON);

    // Test full stick response
    setStickPosition(FD_ROLL, 1.0f);
    setStickPosition(FD_PITCH, -1.0f);
    setStickPosition(FD_YAW, 1.0f);
    simulatedMotorMixRange = 2.0f;
    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());

    // Expect no iterm accumulation
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].I);

    // Test itermWindup limit
    // First store values without exceeding iterm windup limit
    resetTest();
    ENABLE_ARMING_FLAG(ARMED);
    pidStabilisationState(PID_STABILISATION_ON);
    setStickPosition(FD_ROLL, 0.1f);
    setStickPosition(FD_PITCH, -0.1f);
    setStickPosition(FD_YAW, 0.1f);
    simulatedMotorMixRange = 0.0f;
    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());
    float rollTestIterm = pidData[FD_ROLL].I;
    float pitchTestIterm = pidData[FD_PITCH].I;
    float yawTestIterm = pidData[FD_YAW].I;

    // Now compare values when exceeding the limit
    resetTest();
    ENABLE_ARMING_FLAG(ARMED);
    pidStabilisationState(PID_STABILISATION_ON);
    setStickPosition(FD_ROLL, 0.1f);
    setStickPosition(FD_PITCH, -0.1f);
    setStickPosition(FD_YAW, 0.1f);
    simulatedMotorMixRange = (pidProfile->itermWindupPointPercent + 1) / 100.0f;
    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());
    ASSERT_LT(pidData[FD_ROLL].I, rollTestIterm);
    ASSERT_GE(pidData[FD_PITCH].I, pitchTestIterm);
    ASSERT_LT(pidData[FD_YAW].I, yawTestIterm);
}

// TODO - Add more scenarios
TEST(pidControllerTest, testCrashRecoveryMode) {
    resetTest();
    pidProfile->crash_recovery = PID_CRASH_RECOVERY_ON;
    pidInit(pidProfile);
    ENABLE_ARMING_FLAG(ARMED);
    pidStabilisationState(PID_STABILISATION_ON);
    sensorsSet(SENSOR_ACC);

    EXPECT_FALSE(crashRecoveryModeActive());

    int loopsToCrashTime = (int)((pidProfile->crash_time * 1000) / targetPidLooptime) + 1;

    // generate crash detection for roll axis
    gyro.gyroADCf[FD_ROLL]  = 800;
    simulatedMotorMixRange = 1.2f;
    for (int loop =0; loop <= loopsToCrashTime; loop++) {
        gyro.gyroADCf[FD_ROLL] += gyro.gyroADCf[FD_ROLL];
        pidController(pidProfile, &rollAndPitchTrims, currentTestTime());
    }

    EXPECT_TRUE(crashRecoveryModeActive());
    // Add additional verifications
}

TEST(pidControllerTest, testFeedForward) {
    resetTest();
    ENABLE_ARMING_FLAG(ARMED);
    pidStabilisationState(PID_STABILISATION_ON);

    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].F);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].F);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].F);

    // Match the stick to gyro to stop error
    setStickPosition(FD_ROLL, 1.0f);
    setStickPosition(FD_PITCH, -1.0f);
    setStickPosition(FD_YAW, -1.0f);

    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());

    ASSERT_NEAR(2232.78, pidData[FD_ROLL].F, calculateTolerance(2232.78));
    ASSERT_NEAR(-2061.03, pidData[FD_PITCH].F, calculateTolerance(-2061.03));
    ASSERT_NEAR(-82.52, pidData[FD_YAW].F, calculateTolerance(-82.5));

    // Match the stick to gyro to stop error
    setStickPosition(FD_ROLL, 0.5f);
    setStickPosition(FD_PITCH, -0.5f);
    setStickPosition(FD_YAW, -0.5f);

    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());

    ASSERT_NEAR(-558.20, pidData[FD_ROLL].F, calculateTolerance(-558.20));
    ASSERT_NEAR(515.26, pidData[FD_PITCH].F, calculateTolerance(515.26));
    ASSERT_NEAR(-41.26, pidData[FD_YAW].F, calculateTolerance(-41.26));

    for (int loop =0; loop <= 15; loop++) {
        gyro.gyroADCf[FD_ROLL] += gyro.gyroADCf[FD_ROLL];
        pidController(pidProfile, &rollAndPitchTrims, currentTestTime());
    }

    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].F);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].F);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].F);

}

TEST(pidControllerTest, testItermRelax) {
    resetTest();
    pidProfile->iterm_relax = ITERM_RELAX_RPY;
    pidInit(pidProfile);
    ENABLE_ARMING_FLAG(ARMED);
    pidStabilisationState(PID_STABILISATION_ON);

    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());

    // Loop 1 - Expecting zero since there is no error
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].D);

    simulatedSetpointRate[FD_ROLL] = 10;
    simulatedSetpointRate[FD_PITCH] = -10;
    simulatedSetpointRate[FD_YAW] = 10;
    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());
    ASSERT_NEAR(0.52f, pidData[FD_ROLL].I, calculateTolerance(0.52f));
    ASSERT_NEAR(-0.65f, pidData[FD_PITCH].I, calculateTolerance(-0.65f));
    ASSERT_NEAR(0.59f, pidData[FD_YAW].I, calculateTolerance(0.59f));

    // Should stay same when ITERM_RELAX_SETPOINT_THRESHOLD reached
    simulatedSetpointRate[FD_ROLL] = ITERM_RELAX_SETPOINT_THRESHOLD;
    simulatedSetpointRate[FD_PITCH] = -ITERM_RELAX_SETPOINT_THRESHOLD;
    simulatedSetpointRate[FD_YAW] = ITERM_RELAX_SETPOINT_THRESHOLD;
    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());
    ASSERT_NEAR(0.52f, pidData[FD_ROLL].I, calculateTolerance(0.52f));
    ASSERT_NEAR(-0.65f, pidData[FD_PITCH].I, calculateTolerance(-0.65f));
    ASSERT_NEAR(0.59f, pidData[FD_YAW].I, calculateTolerance(0.59f));

    simulatedSetpointRate[FD_ROLL] = 20;
    simulatedSetpointRate[FD_PITCH] = -20;
    simulatedSetpointRate[FD_YAW] = 20;

    pidProfile->iterm_relax_type = ITERM_RELAX_GYRO,
    pidInit(pidProfile);
    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());
    ASSERT_NEAR(0.52f, pidData[FD_ROLL].I, calculateTolerance(0.52f));
    ASSERT_NEAR(-0.65f, pidData[FD_PITCH].I, calculateTolerance(-0.65f));
    ASSERT_NEAR(0.59f, pidData[FD_YAW].I, calculateTolerance(0.59f));

    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());
    ASSERT_NEAR(0.79f, pidData[FD_ROLL].I, calculateTolerance(0.79f));
    ASSERT_NEAR(-0.98f, pidData[FD_PITCH].I, calculateTolerance(-0.98f));
    ASSERT_NEAR(0.88f, pidData[FD_YAW].I, calculateTolerance(0.88));
}

// TODO - Add more tests
TEST(pidControllerTest, testAbsoluteControl) {
    resetTest();
    pidProfile->abs_control_gain = 10;
    pidInit(pidProfile);
    ENABLE_ARMING_FLAG(ARMED);
    pidStabilisationState(PID_STABILISATION_ON);

    // Loop 1 - Expecting zero since there is no error
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].P);
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].I);
    EXPECT_FLOAT_EQ(0, pidData[FD_ROLL].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_PITCH].D);
    EXPECT_FLOAT_EQ(0, pidData[FD_YAW].D);
    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());

    // Add some rotation on ROLL to generate error
    simulatedSetpointRate[FD_ROLL] = 10;
    simulatedSetpointRate[FD_PITCH] = -10;
    simulatedSetpointRate[FD_YAW] = 10;
    pidController(pidProfile, &rollAndPitchTrims, currentTestTime());
    ASSERT_NEAR(12.8, pidData[FD_ROLL].P, calculateTolerance(12.8));
    ASSERT_NEAR(-18.57, pidData[FD_PITCH].P, calculateTolerance(-18.57));
    ASSERT_NEAR(22.4, pidData[FD_YAW].P, calculateTolerance(22.4));
    ASSERT_NEAR(0.84, pidData[FD_ROLL].I, calculateTolerance(0.84));
    ASSERT_NEAR(-0.92, pidData[FD_PITCH].I, calculateTolerance(-0.92));
    ASSERT_NEAR(0.95, pidData[FD_YAW].I, calculateTolerance(-0.95));
}

TEST(pidControllerTest, testDtermFiltering) {
// TODO
}

TEST(pidControllerTest, testItermRotationHandling) {
// TODO
}
