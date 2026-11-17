// =============================================================================
// CadRenderer.WPF / ViewModels / IDrawable.cs
//
// THE VIEW SIDE
//
//     model (CadModel)                view model (here)            engine
//     ----------------                -----------------            ------
//     IEntity  <------ holds -------  ArcVM : IDrawable  ------->  scene node
//     Arc, Edge ...                   Draw()                        (CadViewport)
//     no drawing code                 knows how to show the entity
//
// The viewport keeps a list of IDrawable and Redraw() calls Draw() on each:
//
//     ViewportHost.Drawables.Add(new ArcVM(arc, ViewportHost.Viewport));
//     ...
//     arc.SweepAngleDegrees = 180;      // change the MODEL
//     ViewportHost.Redraw();            // every Draw() runs; the view follows
// =============================================================================

namespace CadRenderer.WPF
{
    public interface IDrawable
    {
        /// <summary>
        /// Brings the picture up to date with the entity: transform and draw.
        /// Called by GLViewportHost.Redraw() before each frame is painted.
        /// </summary>
        void Draw();
    }
}
