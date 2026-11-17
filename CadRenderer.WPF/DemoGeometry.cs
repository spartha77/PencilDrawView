// =============================================================================
// CadRenderer.WPF / DemoGeometry.cs
//
// Small generators for test geometry, in the same array formats the bridge
// expects from a real CAD kernel:
//   positions / normals : x,y,z per vertex        indices : 3 per triangle
//   edgeSegments        : x,y,z,x,y,z per line segment
// =============================================================================
using System;
using System.Collections.Generic;

namespace CadRenderer.WPF
{
    public sealed class MeshData
    {
        public float[] Positions = Array.Empty<float>();
        public float[] Normals = Array.Empty<float>();
        public uint[] Indices = Array.Empty<uint>();
        public float[] EdgeSegments = Array.Empty<float>();
    }

    public static class DemoGeometry
    {
        /// <summary>
        /// A box from (0,0,0) to (sx,sy,sz) the way a B-Rep tessellator delivers
        /// it: each of the 6 faces has its own 4 vertices (flat normals), and the
        /// 12 topological edges are supplied separately for the edge overlay.
        /// </summary>
        public static MeshData Box(float sx, float sy, float sz)
        {
            float[][] corner =
            {
                new[] { 0f, 0f, 0f }, new[] { sx, 0f, 0f }, new[] { sx, sy, 0f }, new[] { 0f, sy, 0f },
                new[] { 0f, 0f, sz }, new[] { sx, 0f, sz }, new[] { sx, sy, sz }, new[] { 0f, sy, sz }
            };
            int[][] faces =
            {
                new[] { 0, 3, 2, 1 }, new[] { 4, 5, 6, 7 }, new[] { 0, 1, 5, 4 },
                new[] { 1, 2, 6, 5 }, new[] { 2, 3, 7, 6 }, new[] { 3, 0, 4, 7 }
            };
            float[][] faceNormals =
            {
                new[] { 0f, 0f, -1f }, new[] { 0f, 0f, 1f }, new[] { 0f, -1f, 0f },
                new[] { 1f, 0f, 0f }, new[] { 0f, 1f, 0f }, new[] { -1f, 0f, 0f }
            };
            int[][] edges =
            {
                new[] { 0, 1 }, new[] { 1, 2 }, new[] { 2, 3 }, new[] { 3, 0 },
                new[] { 4, 5 }, new[] { 5, 6 }, new[] { 6, 7 }, new[] { 7, 4 },
                new[] { 0, 4 }, new[] { 1, 5 }, new[] { 2, 6 }, new[] { 3, 7 }
            };

            var positions = new List<float>();
            var normals = new List<float>();
            var indices = new List<uint>();
            for (int f = 0; f < 6; f++)
            {
                uint first = (uint)(positions.Count / 3);
                foreach (int c in faces[f])
                {
                    positions.AddRange(corner[c]);
                    normals.AddRange(faceNormals[f]);
                }
                indices.AddRange(new[] { first, first + 1, first + 2, first, first + 2, first + 3 });
            }

            var edgeSegments = new List<float>();
            foreach (int[] e in edges)
            {
                edgeSegments.AddRange(corner[e[0]]);
                edgeSegments.AddRange(corner[e[1]]);
            }

            return new MeshData
            {
                Positions = positions.ToArray(),
                Normals = normals.ToArray(),
                Indices = indices.ToArray(),
                EdgeSegments = edgeSegments.ToArray()
            };
        }

        /// <summary>
        /// A wavy sheet of points, coloured from blue (low) to red (high) - a
        /// stand-in for scan data. Returns positions and matching colours.
        /// </summary>
        public static (float[] positions, float[] colors) WavyPointCloud(
            int countX, int countY, float spacingMm, float originX, float originY, float baseZ)
        {
            var positions = new float[countX * countY * 3];
            var colors = new float[countX * countY * 3];
            const float amplitude = 12f;
            int k = 0;
            for (int i = 0; i < countX; i++)
            {
                for (int j = 0; j < countY; j++)
                {
                    float wave = MathF.Sin(i * 0.12f) * MathF.Cos(j * 0.10f);   // -1 .. 1
                    positions[k] = originX + i * spacingMm;
                    positions[k + 1] = originY + j * spacingMm;
                    positions[k + 2] = baseZ + amplitude * wave;

                    float t = (wave + 1f) * 0.5f;                               // 0 .. 1
                    colors[k] = t;
                    colors[k + 1] = 0.35f;
                    colors[k + 2] = 1f - t;
                    k += 3;
                }
            }
            return (positions, colors);
        }

        /// <summary>Column-major 4x4 translation matrix for SetNodeTransform.</summary>
        public static float[] Translation(float x, float y, float z) => new[]
        {
            1f, 0f, 0f, 0f,
            0f, 1f, 0f, 0f,
            0f, 0f, 1f, 0f,
            x,  y,  z,  1f
        };
    }
}
