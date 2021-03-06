/*
 * Copyright (c) 2013 Ambroz Bizjak
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdio.h>

#include <aprinter/platform/teensy3/teensy3_support.h>

static void emergency (void);

#define AMBROLIB_EMERGENCY_ACTION { cli(); emergency(); }
#define AMBROLIB_ABORT_ACTION { while (1); }

#include <aprinter/meta/MakeTypeList.h>
#include <aprinter/meta/Object.h>
#include <aprinter/base/DebugObject.h>
#include <aprinter/system/BusyEventLoop.h>
#include <aprinter/system/Mk20Clock.h>
#include <aprinter/system/Mk20Pins.h>
#include <aprinter/system/InterruptLock.h>
#include <aprinter/system/Mk20Adc.h>
#include <aprinter/system/Mk20Watchdog.h>
#include <aprinter/system/TeensyUsbSerial.h>
#include <aprinter/devices/SpiSdCard.h>
#include <aprinter/printer/PrinterMain.h>
#include <aprinter/printer/thermistor/GenericThermistor.h>
#include <aprinter/printer/temp_control/PidControl.h>
#include <aprinter/printer/temp_control/BinaryControl.h>
#include <aprinter/printer/transform/DeltaTransform.h>
#include <aprinter/printer/teensy3_pins.h>

using namespace APrinter;

static int const AdcADiv = 3;

using LedBlinkInterval = AMBRO_WRAP_DOUBLE(0.5);
using DefaultInactiveTime = AMBRO_WRAP_DOUBLE(60.0);
using SpeedLimitMultiply = AMBRO_WRAP_DOUBLE(1.0 / 60.0);
using MaxStepsPerCycle = AMBRO_WRAP_DOUBLE(0.0017);
using ForceTimeout = AMBRO_WRAP_DOUBLE(0.1);
using TheAxisStepperPrecisionParams = AxisStepperDuePrecisionParams;

using ABCDefaultStepsPerUnit = AMBRO_WRAP_DOUBLE(100.0);
using ABCDefaultMin = AMBRO_WRAP_DOUBLE(0.0);
using ABCDefaultMax = AMBRO_WRAP_DOUBLE(360.0);
using ABCDefaultMaxSpeed = AMBRO_WRAP_DOUBLE(200.0);
using ABCDefaultMaxAccel = AMBRO_WRAP_DOUBLE(9000.0);
using ABCDefaultDistanceFactor = AMBRO_WRAP_DOUBLE(1.0);
using ABCDefaultCorneringDistance = AMBRO_WRAP_DOUBLE(40.0);
using ABCDefaultHomeFastMaxDist = AMBRO_WRAP_DOUBLE(363.0);
using ABCDefaultHomeRetractDist = AMBRO_WRAP_DOUBLE(3.0);
using ABCDefaultHomeSlowMaxDist = AMBRO_WRAP_DOUBLE(5.0);
using ABCDefaultHomeFastSpeed = AMBRO_WRAP_DOUBLE(70.0);
using ABCDefaultHomeRetractSpeed = AMBRO_WRAP_DOUBLE(70.0);
using ABCDefaultHomeSlowSpeed = AMBRO_WRAP_DOUBLE(5.0);

using EDefaultStepsPerUnit = AMBRO_WRAP_DOUBLE(928.0);
using EDefaultMin = AMBRO_WRAP_DOUBLE(-40000.0);
using EDefaultMax = AMBRO_WRAP_DOUBLE(40000.0);
using EDefaultMaxSpeed = AMBRO_WRAP_DOUBLE(45.0);
using EDefaultMaxAccel = AMBRO_WRAP_DOUBLE(250.0);
using EDefaultDistanceFactor = AMBRO_WRAP_DOUBLE(1.0);
using EDefaultCorneringDistance = AMBRO_WRAP_DOUBLE(40.0);

using ExtruderHeaterThermistorResistorR = AMBRO_WRAP_DOUBLE(4700.0);
using ExtruderHeaterThermistorR0 = AMBRO_WRAP_DOUBLE(100000.0);
using ExtruderHeaterThermistorBeta = AMBRO_WRAP_DOUBLE(3960.0);
using ExtruderHeaterThermistorMinTemp = AMBRO_WRAP_DOUBLE(10.0);
using ExtruderHeaterThermistorMaxTemp = AMBRO_WRAP_DOUBLE(300.0);
using ExtruderHeaterMinSafeTemp = AMBRO_WRAP_DOUBLE(20.0);
using ExtruderHeaterMaxSafeTemp = AMBRO_WRAP_DOUBLE(280.0);
using ExtruderHeaterPulseInterval = AMBRO_WRAP_DOUBLE(0.2);
using ExtruderHeaterControlInterval = ExtruderHeaterPulseInterval;
using ExtruderHeaterPidP = AMBRO_WRAP_DOUBLE(0.047);
using ExtruderHeaterPidI = AMBRO_WRAP_DOUBLE(0.0006);
using ExtruderHeaterPidD = AMBRO_WRAP_DOUBLE(0.17);
using ExtruderHeaterPidIStateMin = AMBRO_WRAP_DOUBLE(0.0);
using ExtruderHeaterPidIStateMax = AMBRO_WRAP_DOUBLE(0.4);
using ExtruderHeaterPidDHistory = AMBRO_WRAP_DOUBLE(0.7);
using ExtruderHeaterObserverInterval = AMBRO_WRAP_DOUBLE(0.5);
using ExtruderHeaterObserverTolerance = AMBRO_WRAP_DOUBLE(3.0);
using ExtruderHeaterObserverMinTime = AMBRO_WRAP_DOUBLE(3.0);

using BedHeaterThermistorResistorR = AMBRO_WRAP_DOUBLE(4700.0);
using BedHeaterThermistorR0 = AMBRO_WRAP_DOUBLE(10000.0);
using BedHeaterThermistorBeta = AMBRO_WRAP_DOUBLE(3480.0);
using BedHeaterThermistorMinTemp = AMBRO_WRAP_DOUBLE(10.0);
using BedHeaterThermistorMaxTemp = AMBRO_WRAP_DOUBLE(150.0);
using BedHeaterMinSafeTemp = AMBRO_WRAP_DOUBLE(20.0);
using BedHeaterMaxSafeTemp = AMBRO_WRAP_DOUBLE(120.0);
using BedHeaterPulseInterval = AMBRO_WRAP_DOUBLE(0.3);
using BedHeaterControlInterval = AMBRO_WRAP_DOUBLE(0.3);
using BedHeaterPidP = AMBRO_WRAP_DOUBLE(1.0);
using BedHeaterPidI = AMBRO_WRAP_DOUBLE(0.012);
using BedHeaterPidD = AMBRO_WRAP_DOUBLE(2.5);
using BedHeaterPidIStateMin = AMBRO_WRAP_DOUBLE(0.0);
using BedHeaterPidIStateMax = AMBRO_WRAP_DOUBLE(1.0);
using BedHeaterPidDHistory = AMBRO_WRAP_DOUBLE(0.8);
using BedHeaterObserverInterval = AMBRO_WRAP_DOUBLE(0.5);
using BedHeaterObserverTolerance = AMBRO_WRAP_DOUBLE(1.5);
using BedHeaterObserverMinTime = AMBRO_WRAP_DOUBLE(3.0);

using FanSpeedMultiply = AMBRO_WRAP_DOUBLE(1.0 / 255.0);
using FanPulseInterval = AMBRO_WRAP_DOUBLE(0.04);

using DeltaDiagonalRod = AMBRO_WRAP_DOUBLE(214.0);
using DeltaSmoothRodOffset = AMBRO_WRAP_DOUBLE(145.0);
using DeltaEffectorOffset = AMBRO_WRAP_DOUBLE(19.9);
using DeltaCarriageOffset = AMBRO_WRAP_DOUBLE(19.5);
using DeltaRadius = AMBRO_WRAP_DOUBLE(DeltaSmoothRodOffset::value() - DeltaEffectorOffset::value() - DeltaCarriageOffset::value());
using DeltaSegmentsPerSecond = AMBRO_WRAP_DOUBLE(100.0);
using DeltaMinSplitLength = AMBRO_WRAP_DOUBLE(0.1);
using DeltaMaxSplitLength = AMBRO_WRAP_DOUBLE(4.0);
using DeltaTower1X = AMBRO_WRAP_DOUBLE(DeltaRadius::value() * -0.8660254037844386);
using DeltaTower1Y = AMBRO_WRAP_DOUBLE(DeltaRadius::value() * -0.5);
using DeltaTower2X = AMBRO_WRAP_DOUBLE(DeltaRadius::value() * 0.8660254037844386);
using DeltaTower2Y = AMBRO_WRAP_DOUBLE(DeltaRadius::value() * -0.5);
using DeltaTower3X = AMBRO_WRAP_DOUBLE(DeltaRadius::value() * 0.0);
using DeltaTower3Y = AMBRO_WRAP_DOUBLE(DeltaRadius::value() * 1.0);

using XMaxSpeed = AMBRO_WRAP_DOUBLE(INFINITY);
using YMaxSpeed = AMBRO_WRAP_DOUBLE(INFINITY);
using ZMaxSpeed = AMBRO_WRAP_DOUBLE(INFINITY);

using PrinterParams = PrinterMainParams<
    /*
     * Common parameters.
     */
    PrinterMainSerialParams<
        UINT32_C(0), // BaudRate,
        8, // RecvBufferSizeExp
        8, // SendBufferSizeExp
        GcodeParserParams<16>, // ReceiveBufferSizeExp
        TeensyUsbSerial,
        TeensyUsbSerialParams
    >,
    TeensyPin13, // LedPin
    LedBlinkInterval, // LedBlinkInterval
    DefaultInactiveTime, // DefaultInactiveTime
    SpeedLimitMultiply, // SpeedLimitMultiply
    MaxStepsPerCycle, // MaxStepsPerCycle
    32, // StepperSegmentBufferSize
    32, // EventChannelBufferSize
    28, // LookaheadBufferSize
    10, // LookaheadCommitCount
    ForceTimeout, // ForceTimeout
    float, // FpType
    Mk20ClockInterruptTimer_Ftm0_Ch0, // EventChannelTimer
    Mk20Watchdog,
    Mk20WatchdogParams<2000, 0>,
    PrinterMainNoSdCardParams,
    PrinterMainNoProbeParams,
    PrinterMainNoCurrentParams,
    
    /*
     * Axes.
     */
    MakeTypeList<
        PrinterMainAxisParams<
            'A', // Name
            TeensyPin1, // DirPin
            TeensyPin0, // StepPin
            TeensyPin8, // EnablePin
            true, // InvertDir
            ABCDefaultStepsPerUnit, // StepsPerUnit
            ABCDefaultMin, // Min
            ABCDefaultMax, // Max
            ABCDefaultMaxSpeed, // MaxSpeed
            ABCDefaultMaxAccel, // MaxAccel
            ABCDefaultDistanceFactor, // DistanceFactor
            ABCDefaultCorneringDistance, // CorneringDistance
            PrinterMainHomingParams<
                TeensyPin12, // HomeEndPin
                Mk20PinInputModePullUp, // HomeEndPinInputMode
                false, // HomeEndInvert
                true, // HomeDir
                ABCDefaultHomeFastMaxDist, // HomeFastMaxDist
                ABCDefaultHomeRetractDist, // HomeRetractDist
                ABCDefaultHomeSlowMaxDist, // HomeSlowMaxDist
                ABCDefaultHomeFastSpeed, // HomeFastSpeed
                ABCDefaultHomeRetractSpeed, // HomeRetractSpeed
                ABCDefaultHomeSlowSpeed // HomeSlowSpeed
            >,
            false, // EnableCartesianSpeedLimit
            32, // StepBits
            AxisStepperParams<
                Mk20ClockInterruptTimer_Ftm0_Ch1, // StepperTimer,
                TheAxisStepperPrecisionParams // PrecisionParams
            >,
            PrinterMainNoMicroStepParams
        >,
        PrinterMainAxisParams<
            'B', // Name
            TeensyPin3, // DirPin
            TeensyPin2, // StepPin
            TeensyPin9, // EnablePin
            true, // InvertDir
            ABCDefaultStepsPerUnit, // StepsPerUnit
            ABCDefaultMin, // Min
            ABCDefaultMax, // Max
            ABCDefaultMaxSpeed, // MaxSpeed
            ABCDefaultMaxAccel, // MaxAccel
            ABCDefaultDistanceFactor, // DistanceFactor
            ABCDefaultCorneringDistance, // CorneringDistance
            PrinterMainHomingParams<
                TeensyPin14, // HomeEndPin
                Mk20PinInputModePullUp, // HomeEndPinInputMode
                false, // HomeEndInvert
                true, // HomeDir
                ABCDefaultHomeFastMaxDist, // HomeFastMaxDist
                ABCDefaultHomeRetractDist, // HomeRetractDist
                ABCDefaultHomeSlowMaxDist, // HomeSlowMaxDist
                ABCDefaultHomeFastSpeed, // HomeFastSpeed
                ABCDefaultHomeRetractSpeed, // HomeRetractSpeed
                ABCDefaultHomeSlowSpeed // HomeSlowSpeed
            >,
            false, // EnableCartesianSpeedLimit
            32, // StepBits
            AxisStepperParams<
                Mk20ClockInterruptTimer_Ftm0_Ch2, // StepperTimer
                TheAxisStepperPrecisionParams // PrecisionParams
            >,
            PrinterMainNoMicroStepParams
        >,
        PrinterMainAxisParams<
            'C', // Name
            TeensyPin5, // DirPin
            TeensyPin4, // StepPin
            TeensyPin10, // EnablePin
            false, // InvertDir
            ABCDefaultStepsPerUnit, // StepsPerUnit
            ABCDefaultMin, // Min
            ABCDefaultMax, // Max
            ABCDefaultMaxSpeed, // MaxSpeed
            ABCDefaultMaxAccel, // MaxAccel
            ABCDefaultDistanceFactor, // DistanceFactor
            ABCDefaultCorneringDistance, // CorneringDistance
            PrinterMainHomingParams<
                TeensyPin15, // HomeEndPin
                Mk20PinInputModePullUp, // HomeEndPinInputMode
                false, // HomeEndInvert
                true, // HomeDir
                ABCDefaultHomeFastMaxDist, // HomeFastMaxDist
                ABCDefaultHomeRetractDist, // HomeRetractDist
                ABCDefaultHomeSlowMaxDist, // HomeSlowMaxDist
                ABCDefaultHomeFastSpeed, // HomeFastSpeed
                ABCDefaultHomeRetractSpeed, // HomeRetractSpeed
                ABCDefaultHomeSlowSpeed // HomeSlowSpeed
            >,
            false, // EnableCartesianSpeedLimit
            32, // StepBits
            AxisStepperParams<
                Mk20ClockInterruptTimer_Ftm0_Ch3, // StepperTimer
                TheAxisStepperPrecisionParams // PrecisionParams
            >,
            PrinterMainNoMicroStepParams
        >,
        PrinterMainAxisParams<
            'E', // Name
            TeensyPin7, // DirPin
            TeensyPin6, // StepPin
            TeensyPin11, // EnablePin
            true, // InvertDir
            EDefaultStepsPerUnit, // StepsPerUnit
            EDefaultMin, // Min
            EDefaultMax, // Max
            EDefaultMaxSpeed, // MaxSpeed
            EDefaultMaxAccel, // MaxAccel
            EDefaultDistanceFactor, // DistanceFactor
            EDefaultCorneringDistance, // CorneringDistance
            PrinterMainNoHomingParams,
            false, // EnableCartesianSpeedLimit
            32, // StepBits
            AxisStepperParams<
                Mk20ClockInterruptTimer_Ftm0_Ch4, // StepperTimer
                TheAxisStepperPrecisionParams // PrecisionParams
            >,
            PrinterMainNoMicroStepParams
        >
    >,
    
    /*
     * Transform and virtual axes.
     */
    PrinterMainTransformParams<
        MakeTypeList<
            PrinterMainVirtualAxisParams<
                'X', // Name
                XMaxSpeed
            >,
            PrinterMainVirtualAxisParams<
                'Y', // Name
                YMaxSpeed
            >,
            PrinterMainVirtualAxisParams<
                'Z', // Name
                ZMaxSpeed
            >
        >,
        MakeTypeList<WrapInt<'A'>, WrapInt<'B'>, WrapInt<'C'>>,
        DeltaSegmentsPerSecond,
        DeltaTransform,
        DeltaTransformParams<
            DeltaDiagonalRod,
            DeltaTower1X,
            DeltaTower1Y,
            DeltaTower2X,
            DeltaTower2Y,
            DeltaTower3X,
            DeltaTower3Y,
            DistanceSplitterParams<DeltaMinSplitLength, DeltaMaxSplitLength>
        >
    >,
    
    /*
     * Heaters.
     */
    MakeTypeList<
        PrinterMainHeaterParams<
            'T', // Name
            104, // SetMCommand
            109, // WaitMCommand
            301, // SetConfigMCommand
            TeensyPinA2, // AdcPin
            TeensyPin17, // OutputPin
            true, // OutputInvert
            GenericThermistor< // Thermistor
                ExtruderHeaterThermistorResistorR,
                ExtruderHeaterThermistorR0,
                ExtruderHeaterThermistorBeta,
                ExtruderHeaterThermistorMinTemp,
                ExtruderHeaterThermistorMaxTemp
            >,
            ExtruderHeaterMinSafeTemp, // MinSafeTemp
            ExtruderHeaterMaxSafeTemp, // MaxSafeTemp
            ExtruderHeaterPulseInterval, // PulseInterval
            ExtruderHeaterControlInterval, // ControlInterval
            PidControl, // Control
            PidControlParams<
                ExtruderHeaterPidP, // PidP
                ExtruderHeaterPidI, // PidI
                ExtruderHeaterPidD, // PidD
                ExtruderHeaterPidIStateMin, // PidIStateMin
                ExtruderHeaterPidIStateMax, // PidIStateMax
                ExtruderHeaterPidDHistory // PidDHistory
            >,
            TemperatureObserverParams<
                ExtruderHeaterObserverInterval, // ObserverInterval
                ExtruderHeaterObserverTolerance, // ObserverTolerance
                ExtruderHeaterObserverMinTime // ObserverMinTime
            >,
            Mk20ClockInterruptTimer_Ftm0_Ch5 // TimerTemplate
        >
#if 0
        PrinterMainHeaterParams<
            'B', // Name
            140, // SetMCommand
            190, // WaitMCommand
            304, // SetConfigMCommand
            NOOONE, // AdcPin
            NOOONE, // OutputPin
            true, // OutputInvert
            GenericThermistor< // Thermistor
                BedHeaterThermistorResistorR,
                BedHeaterThermistorR0,
                BedHeaterThermistorBeta,
                BedHeaterThermistorMinTemp,
                BedHeaterThermistorMaxTemp
            >,
            BedHeaterMinSafeTemp, // MinSafeTemp
            BedHeaterMaxSafeTemp, // MaxSafeTemp
            BedHeaterPulseInterval, // PulseInterval
            BedHeaterControlInterval, // ControlInterval
            PidControl, // Control
            PidControlParams<
                BedHeaterPidP, // PidP
                BedHeaterPidI, // PidI
                BedHeaterPidD, // PidD
                BedHeaterPidIStateMin, // PidIStateMin
                BedHeaterPidIStateMax, // PidIStateMax
                BedHeaterPidDHistory // PidDHistory
            >,
            TemperatureObserverParams<
                BedHeaterObserverInterval, // ObserverInterval
                BedHeaterObserverTolerance, // ObserverTolerance
                BedHeaterObserverMinTime // ObserverMinTime
            >,
            Mk20ClockInterruptTimer_Ftm0_Ch6 // TimerTemplate
        >
#endif
    >,
    
    /*
     * Fans.
     */
    MakeTypeList<
        PrinterMainFanParams<
            106, // SetMCommand
            107, // OffMCommand
            TeensyPin18, // OutputPin
            false, // OutputInvert
            FanPulseInterval, // PulseInterval
            FanSpeedMultiply, // SpeedMultiply
            Mk20ClockInterruptTimer_Ftm0_Ch7 // TimerTemplate
        >
    >
>;

// need to list all used ADC pins here
using AdcPins = MakeTypeList<TeensyPinA2>;

static const int clock_timer_prescaler = 4;
using ClockFtmsList = MakeTypeList<Mk20ClockFTM0, Mk20ClockFTM1>;

struct MyContext;
struct MyLoopExtraDelay;
struct Program;

using MyDebugObjectGroup = DebugObjectGroup<MyContext, Program>;
using MyClock = Mk20Clock<MyContext, Program, clock_timer_prescaler, ClockFtmsList>;
using MyLoop = BusyEventLoop<MyContext, Program, MyLoopExtraDelay>;
using MyPins = Mk20Pins<MyContext, Program>;
using MyAdc = Mk20Adc<MyContext, Program, AdcPins, AdcADiv>;
using MyPrinter = PrinterMain<MyContext, Program, PrinterParams>;

struct MyContext {
    using DebugGroup = MyDebugObjectGroup;
    using Clock = MyClock;
    using EventLoop = MyLoop;
    using Pins = MyPins;
    using Adc = MyAdc;
    
    void check () const;
};

using MyLoopExtra = BusyEventLoopExtra<Program, MyLoop, typename MyPrinter::EventLoopFastEvents>;
struct MyLoopExtraDelay : public WrapType<MyLoopExtra> {};

struct Program : public ObjBase<void, void, MakeTypeList<
    MyDebugObjectGroup,
    MyClock,
    MyLoop,
    MyPins,
    MyAdc,
    MyPrinter,
    MyLoopExtra
>> {
    static Program * self (MyContext c);
};

Program p;

Program * Program::self (MyContext c) { return &p; }
void MyContext::check () const {}

AMBRO_MK20_CLOCK_FTM0_GLOBAL(MyClock, MyContext())
AMBRO_MK20_CLOCK_FTM1_GLOBAL(MyClock, MyContext())

AMBRO_MK20_WATCHDOG_GLOBAL(MyPrinter::GetWatchdog)
AMBRO_MK20_ADC_ISRS(MyAdc, MyContext())
AMBRO_MK20_CLOCK_INTERRUPT_TIMER_FTM0_CH0_GLOBAL(MyPrinter::GetEventChannelTimer, MyContext())
AMBRO_MK20_CLOCK_INTERRUPT_TIMER_FTM0_CH1_GLOBAL(MyPrinter::GetAxisTimer<0>, MyContext())
AMBRO_MK20_CLOCK_INTERRUPT_TIMER_FTM0_CH2_GLOBAL(MyPrinter::GetAxisTimer<1>, MyContext())
AMBRO_MK20_CLOCK_INTERRUPT_TIMER_FTM0_CH3_GLOBAL(MyPrinter::GetAxisTimer<2>, MyContext())
AMBRO_MK20_CLOCK_INTERRUPT_TIMER_FTM0_CH4_GLOBAL(MyPrinter::GetAxisTimer<3>, MyContext())
AMBRO_MK20_CLOCK_INTERRUPT_TIMER_FTM0_CH5_GLOBAL(MyPrinter::GetHeaterTimer<0>, MyContext())
//AMBRO_MK20_CLOCK_INTERRUPT_TIMER_FTM0_CH6_GLOBAL(MyPrinter::GetHeaterTimer<1>, MyContext())
AMBRO_MK20_CLOCK_INTERRUPT_TIMER_FTM0_CH7_GLOBAL(MyPrinter::GetFanTimer<0>, MyContext())

static void emergency (void)
{
    MyPrinter::emergency();
}

extern "C" { void usb_init (void); }

int main ()
{
    usb_init();
    
    MyContext c;
    
    MyDebugObjectGroup::init(c);
    MyClock::init(c);
    MyLoop::init(c);
    MyPins::init(c);
    MyAdc::init(c);
    MyPrinter::init(c);
    
    MyLoop::run(c);
}
