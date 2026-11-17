// =============================================================================
// CadRenderer.WPF / ViewModels / NodeVM.cs
//
// A drawable for something that already IS a scene node - a mesh, an STL part,
// a point cloud or a group (assembly) - and only needs to be placed.
// (This replaces the earlier Entity class; Entity.cs can be deleted.)
//
// To make such a part move, derive from NodeVM and override Draw:
//
//     class Conveyor : NodeVM
//     {
//         public double Position;                         // your own state
//         public Conveyor(CadViewport viewport, int nodeId) : base(viewport, nodeId) { }
//
//         public override void Draw()
//         {
//             Matrix = Transform.Translation((float)Position, 0f, 0f);   // transform ...
//             base.Draw();                                               // ... and draw
//         }
//     }
//
// WHAT "DRAW" DOES UNDER THE HOOD
//   Draw() does not send triangles to the graphics card itself. The geometry
//   already lives in graphics memory (uploaded once). Draw() tells the engine
//   where the part is for this frame; Redraw() then lets the engine paint
//   everything in one go - which it must, because the order matters: solid
//   surfaces first, edge lines on top, transparent parts last and sorted back
//   to front, then the selection highlight.
// =============================================================================
using System;
using CadBridge;

namespace CadRenderer.WPF
{
    public class NodeVM : IDrawable
    {
        /// <param name="nodeId">the ID returned by AddMesh, AddStlFile, AddPointCloud or AddGroup</param>
        public NodeVM(CadViewport viewport, int nodeId)
        {
            Viewport = viewport ?? throw new ArgumentNullException(nameof(viewport));
            NodeId = nodeId;
        }

        protected CadViewport Viewport { get; }
        public int NodeId { get; }

        /// <summary>
        /// Where the part is: a 4x4 matrix as 16 floats (see Transform.cs),
        /// relative to the node's parent in the scene tree.
        /// </summary>
        public float[] Matrix { get; set; } = Transform.Identity();

        /// <summary>
        /// Transform and draw. The base version hands <see cref="Matrix"/> to the
        /// engine. Override it, set Matrix from your own state, then call base.Draw().
        /// </summary>
        public virtual void Draw()
        {
            Viewport.SetNodeTransform(NodeId, Matrix);
        }
    }
}
