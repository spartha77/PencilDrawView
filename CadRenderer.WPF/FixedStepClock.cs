// =============================================================================
// CadRenderer.WPF / FixedStepClock.cs
//
// WHY THIS EXISTS
//   The simulation callback runs once per displayed frame, and the display
//   decides how often that is: 60 Hz on one monitor, 144 Hz on another, and
//   it drops when the machine is busy. A simulation that must advance at an
//   exact rate (say physics or a machine controller at 128 Hz) cannot use the
//   frame rate as its clock.
//
//   The standard answer is a FIXED TIME STEP: collect the real elapsed time and
//   run the simulation in equal-sized steps, as many as fit. The remainder is
//   carried over to the next frame, so no time is lost or invented.
//
//       var clock = new FixedStepClock(128);            // 128 steps per second
//       void OnContinuousRender(long elapsedMicroseconds)
//       {
//           int steps = clock.Advance(elapsedMicroseconds);
//           for (int i = 0; i < steps; i++) AdvanceMyModel(clock.StepSeconds);
//           // ... then copy the model's positions into the scene (SetNodeTransform)
//       }
//
//   At 60 Hz this gives 2 steps on most frames and 3 on some; at 144 Hz it
//   gives 1 on most frames and 0 on some. The model sees exactly 128 Hz either way.
// =============================================================================
using System;

namespace CadRenderer.WPF
{
    public sealed class FixedStepClock
    {
        private long _carriedMicroseconds;      // time not yet turned into a step

        public FixedStepClock(double stepsPerSecond)
        {
            if (stepsPerSecond <= 0) throw new ArgumentOutOfRangeException(nameof(stepsPerSecond));
            StepMicroseconds = Math.Max(1, (long)Math.Round(1_000_000.0 / stepsPerSecond));
        }

        /// <summary>Length of one step in microseconds.</summary>
        public long StepMicroseconds { get; }

        /// <summary>Length of one step in seconds (what most formulas want).</summary>
        public double StepSeconds => StepMicroseconds / 1_000_000.0;

        /// <summary>
        /// Upper limit of steps returned by one Advance call. If the simulation is
        /// slower than real time, running ever more steps to catch up would make
        /// it slower still; beyond this limit the backlog is dropped instead.
        /// </summary>
        public int MaxStepsPerCall { get; set; } = 8;

        /// <summary>
        /// Adds the elapsed real time and returns how many whole steps to run now.
        /// </summary>
        public int Advance(long elapsedMicroseconds)
        {
            if (elapsedMicroseconds > 0) _carriedMicroseconds += elapsedMicroseconds;

            long steps = _carriedMicroseconds / StepMicroseconds;
            if (steps > MaxStepsPerCall)
            {
                steps = MaxStepsPerCall;
                _carriedMicroseconds = 0;                       // drop the backlog
            }
            else
            {
                _carriedMicroseconds -= steps * StepMicroseconds;
            }
            return (int)steps;
        }

        /// <summary>Forgets carried-over time (call when a simulation restarts).</summary>
        public void Reset() => _carriedMicroseconds = 0;
    }
}
