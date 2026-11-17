// =============================================================================
// CadRenderer.WPF / Model / Arc.cs
//
// A circular arc - a MODEL entity. It has no Draw method and knows nothing
// about the graphics engine; ArcVM (ViewModels folder) draws it.
//
// The arc lies in a plane given by its centre and two directions:
//
//        YAxis ^
//              |       . end   (StartAngle + SweepAngle)
//              |    .
//              |  .   Radius
//              | .
//       Center +--------. start (StartAngle) ----> XAxis
//
// Angles are in degrees, measured from XAxis towards YAxis. A positive sweep
// runs counter-clockwise seen from the plane's normal (XAxis x YAxis). A sweep
// of 360 (or more) is a full circle. Lengths are in millimetres.
// =============================================================================
using System;

namespace CadModel
{
    /// <summary>A point or a direction in space (mm).</summary>
    public readonly struct Point3 : IEquatable<Point3>
    {
        public Point3(double x, double y, double z) { X = x; Y = y; Z = z; }

        public double X { get; }
        public double Y { get; }
        public double Z { get; }

        public bool Equals(Point3 other) => X == other.X && Y == other.Y && Z == other.Z;
        public override bool Equals(object? obj) => obj is Point3 other && Equals(other);
        public override int GetHashCode() => HashCode.Combine(X, Y, Z);
        public override string ToString() => $"({X}, {Y}, {Z})";
    }

    public sealed class Arc : IGeometry
    {
        /// <summary>An arc in a plane parallel to the XY plane.</summary>
        public Arc(Point3 center, double radius, double startAngleDegrees, double sweepAngleDegrees)
        {
            Center = center;
            Radius = radius;
            StartAngleDegrees = startAngleDegrees;
            SweepAngleDegrees = sweepAngleDegrees;
        }

        public string Name { get; set; } = "Arc";

        public Point3 Center { get; set; }
        public double Radius { get; set; }
        public double StartAngleDegrees { get; set; }
        public double SweepAngleDegrees { get; set; }

        /// <summary>Direction of angle 0. Must be a unit vector.</summary>
        public Point3 XAxis { get; set; } = new Point3(1, 0, 0);

        /// <summary>Direction of angle 90. Must be a unit vector at right angles to XAxis.</summary>
        public Point3 YAxis { get; set; } = new Point3(0, 1, 0);

        /// <summary>True when the sweep covers the whole circle.</summary>
        public bool IsFullCircle => Math.Abs(SweepAngleDegrees) >= 360.0;

        /// <summary>The point of the arc at an angle (degrees from XAxis).</summary>
        public Point3 PointAtAngle(double angleDegrees)
        {
            double a = angleDegrees * Math.PI / 180.0;
            double c = Radius * Math.Cos(a), s = Radius * Math.Sin(a);
            return new Point3(Center.X + c * XAxis.X + s * YAxis.X,
                              Center.Y + c * XAxis.Y + s * YAxis.Y,
                              Center.Z + c * XAxis.Z + s * YAxis.Z);
        }

        public Point3 StartPoint => PointAtAngle(StartAngleDegrees);
        public Point3 EndPoint => PointAtAngle(StartAngleDegrees + SweepAngleDegrees);
    }
}
