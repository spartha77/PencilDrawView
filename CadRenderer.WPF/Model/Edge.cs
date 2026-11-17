// =============================================================================
// CadRenderer.WPF / Model / Edge.cs
//
// An edge - a TOPOLOGICAL entity. It does not describe a shape itself; it is a
// container for the curve it lies on (an Arc, a Line ...). Like every model
// entity it knows nothing about drawing.
//
//     var arc  = new Arc(new Point3(0, 0, 0), 50, 0, 90);
//     var edge = new Edge(arc);          // edge.Geometry is the arc
//
// A view model accepts either: new ArcVM(arc, ...) and new ArcVM(edge, ...)
// draw the same arc.
// =============================================================================
using System;

namespace CadModel
{
    public sealed class Edge : ITopology
    {
        public Edge(IGeometry curve)
        {
            Geometry = curve ?? throw new ArgumentNullException(nameof(curve));
            Name = curve.Name;
        }

        public string Name { get; set; }

        /// <summary>The curve this edge lies on.</summary>
        public IGeometry Geometry { get; }
    }
}
