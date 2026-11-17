// =============================================================================
// CadRenderer.WPF / Model / IEntity.cs
//
// THE MODEL SIDE
//
//   IEntity                     everything in the model: geometry and topology
//    |
//    +-- IGeometry              a shape described by numbers: Arc, (Line, Circle,
//    |                          Plane, Cylinder ... follow the same pattern)
//    |
//    +-- ITopology              a CONTAINER: it holds the geometry it lies on.
//                               Edge holds a curve; (Vertex holds a point, Face
//                               holds a surface ... follow the same pattern)
//
// Nothing in this folder knows how to draw. There is no "using CadBridge" here
// and no Draw method: a model entity is pure data, and can be used, saved and
// tested without a graphics card. Drawing lives in the ViewModels folder:
// a view model takes an IEntity and implements IDrawable for it (see ArcVM.cs).
// =============================================================================

namespace CadModel
{
    /// <summary>Base of every geometric and topological entity.</summary>
    public interface IEntity
    {
        string Name { get; set; }
    }

    /// <summary>A shape described by numbers (an arc, a line, a surface ...).</summary>
    public interface IGeometry : IEntity
    {
    }

    /// <summary>
    /// A topological entity: a container for the geometry it lies on.
    /// (An edge is "this piece of that curve"; the curve is its geometry.)
    /// </summary>
    public interface ITopology : IEntity
    {
        /// <summary>The geometry this entity holds.</summary>
        IGeometry Geometry { get; }
    }
}
