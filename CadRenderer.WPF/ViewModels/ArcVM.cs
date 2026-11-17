// =============================================================================
// CadRenderer.WPF / ViewModels / ArcVM.cs
//
// VIEW MODEL OF AN ARC
//
// The Arc (model) does not know how to draw. ArcVM takes the entity in its
// constructor, BUILDS what is needed to show it, and implements Draw():
//
//     var arc = new Arc(new Point3(0, 0, 0), radius: 50, startAngleDegrees: 0, sweepAngleDegrees: 90);
//     var vm  = new ArcVM(arc, ViewportHost.Viewport);
//     ViewportHost.Drawables.Add(vm);
//
// The constructor takes an IEntity, so it accepts
//   - the geometry itself:        new ArcVM(arc, viewport)
//   - a topology container:       new ArcVM(edge, viewport)   where edge.Geometry is an Arc
//
// WHAT IS BUILT
//   The graphics card draws straight lines, not arcs. So the arc is cut into
//   short straight pieces ("tessellated") - so short that the eye sees a smooth
//   curve: no piece strays more than ChordToleranceMm from the true arc. The
//   points are handed to the engine once (AddPolyline) and stay in graphics
//   memory.
//
// WHAT Draw() DOES
//   1. If the arc's numbers changed since the last Draw(), cut it into pieces
//      again and replace the points in the engine. If nothing changed, this
//      costs a few comparisons and nothing else.
//   2. Hand the placement (Matrix) to the engine.
//   The frame itself is painted by Redraw() after every Draw() has run.
// =============================================================================
using System;
using CadBridge;
using CadModel;

namespace CadRenderer.WPF
{
    public sealed class ArcVM : IDrawable
    {
        private readonly CadViewport _viewport;

        // The arc's numbers as of the last tessellation - to notice a change.
        private Point3 _center, _xAxis, _yAxis;
        private double _radius, _start, _sweep, _tolerance;

        /// <param name="entity">an Arc, or a topology entity (Edge) that holds an Arc</param>
        /// <param name="viewport">the view to draw into</param>
        /// <param name="parentNodeId">scene-tree node to put the arc under (a group); default: the root</param>
        public ArcVM(IEntity entity, CadViewport viewport, int parentNodeId = CadViewport.RootId)
        {
            Entity = entity ?? throw new ArgumentNullException(nameof(entity));
            _viewport = viewport ?? throw new ArgumentNullException(nameof(viewport));

            // A topology entity is a container: look inside for its geometry.
            IGeometry? geometry = entity is ITopology topology ? topology.Geometry : entity as IGeometry;
            Arc = geometry as Arc
                  ?? throw new ArgumentException("ArcVM needs an Arc, or a topology entity that holds an Arc.", nameof(entity));

            // Build: tessellate and create the scene node.
            NodeId = _viewport.AddPolyline(parentNodeId, entity.Name, Tessellate(), Arc.IsFullCircle, WidthPx);
            _viewport.SetNodeColor(NodeId, 0.95f, 0.75f, 0.20f);
        }

        /// <summary>The entity given to the constructor (the Arc or its container).</summary>
        public IEntity Entity { get; }

        /// <summary>The arc that is drawn.</summary>
        public Arc Arc { get; }

        /// <summary>The scene node that shows the arc (for the scene tree, picking, show / hide).</summary>
        public int NodeId { get; }

        /// <summary>Line width on screen, in pixels (used when the VM is created).</summary>
        public float WidthPx { get; } = 2.5f;

        /// <summary>
        /// Placement of the arc as a whole: a 4x4 matrix as 16 floats (see
        /// Transform.cs), relative to the parent node. Identity = where the model says.
        /// </summary>
        public float[] Matrix { get; set; } = Transform.Identity();

        /// <summary>
        /// Largest allowed gap (mm) between the drawn straight pieces and the
        /// true arc. Smaller = smoother and more points.
        /// </summary>
        public double ChordToleranceMm { get; set; } = 0.01;

        public void SetColor(float r, float g, float b) => _viewport.SetNodeColor(NodeId, r, g, b);

        /// <summary>Transform and draw (see the top of this file).</summary>
        public void Draw()
        {
            if (ModelChanged())
                _viewport.SetPolylinePoints(NodeId, Tessellate(), Arc.IsFullCircle);

            // Returns false if the node was removed from the scene meanwhile;
            // such an arc simply no longer shows.
            _viewport.SetNodeTransform(NodeId, Matrix);
        }

        private bool ModelChanged() =>
            Arc.Radius != _radius || Arc.StartAngleDegrees != _start || Arc.SweepAngleDegrees != _sweep ||
            ChordToleranceMm != _tolerance ||
            !Arc.Center.Equals(_center) || !Arc.XAxis.Equals(_xAxis) || !Arc.YAxis.Equals(_yAxis);

        /// <summary>Cuts the arc into points (x,y,z each) and remembers the numbers used.</summary>
        private float[] Tessellate()
        {
            _center = Arc.Center; _xAxis = Arc.XAxis; _yAxis = Arc.YAxis;
            _radius = Arc.Radius; _start = Arc.StartAngleDegrees; _sweep = Arc.SweepAngleDegrees;
            _tolerance = ChordToleranceMm;

            double radius = Math.Abs(Arc.Radius);
            double sweep = Math.Clamp(Arc.SweepAngleDegrees, -360.0, 360.0);

            // A straight piece that spans the angle s bulges away from the arc by
            // radius * (1 - cos(s / 2)). Solve for the s that gives the tolerance.
            double stepDegrees = 15.0;                                   // coarse fallback for tiny radii
            double tolerance = Math.Max(ChordToleranceMm, 1e-6);
            if (radius > tolerance)
                stepDegrees = 2.0 * Math.Acos(1.0 - tolerance / radius) * 180.0 / Math.PI;
            stepDegrees = Math.Clamp(stepDegrees, 0.25, 15.0);           // 24 .. 1440 pieces per full turn

            int pieces = Math.Max(2, (int)Math.Ceiling(Math.Abs(sweep) / stepDegrees));

            // A full circle is "closed": the engine joins the last point to the
            // first, so the first point is not repeated at the end.
            int count = Arc.IsFullCircle ? pieces : pieces + 1;
            var points = new float[count * 3];
            for (int i = 0; i < count; ++i)
            {
                Point3 p = Arc.PointAtAngle(Arc.StartAngleDegrees + sweep * i / pieces);
                points[i * 3] = (float)p.X;
                points[i * 3 + 1] = (float)p.Y;
                points[i * 3 + 2] = (float)p.Z;
            }
            return points;
        }
    }
}
