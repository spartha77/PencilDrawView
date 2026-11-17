// =============================================================================
// CadRenderer.WPF / Transform.cs
//
// Builds the 4x4 matrices that CadViewport.SetNodeTransform expects.
//
// LAYOUT: 16 floats, COLUMN-major (the OpenGL convention):
//        [ m0  m4  m8   m12 ]      m12, m13, m14 = translation x, y, z
//        [ m1  m5  m9   m13 ]      upper-left 3x3 = rotation / scale
//        [ m2  m6  m10  m14 ]
//        [ m3  m7  m11  m15 ]
//
// COMBINING: Multiply(a, b) means "do b first, then a". To turn a part about
// its own centre c and then place it at p:
//        Multiply(Translation(p), Multiply(RotationZ(angle), Translation(-c)))
// =============================================================================
using System;

namespace CadRenderer.WPF
{
    public static class Transform
    {
        public static float[] Identity() => new[]
        {
            1f, 0f, 0f, 0f,
            0f, 1f, 0f, 0f,
            0f, 0f, 1f, 0f,
            0f, 0f, 0f, 1f
        };

        public static float[] Translation(float x, float y, float z)
        {
            float[] m = Identity();
            m[12] = x; m[13] = y; m[14] = z;
            return m;
        }

        public static float[] Scale(float factor)
        {
            float[] m = Identity();
            m[0] = m[5] = m[10] = factor;
            return m;
        }

        /// <summary>Rotation about the X axis; positive = counter-clockwise seen from +X.</summary>
        public static float[] RotationX(float degrees)
        {
            (float s, float c) = SinCos(degrees);
            float[] m = Identity();
            m[5] = c; m[6] = s;
            m[9] = -s; m[10] = c;
            return m;
        }

        /// <summary>Rotation about the Y axis; positive = counter-clockwise seen from +Y.</summary>
        public static float[] RotationY(float degrees)
        {
            (float s, float c) = SinCos(degrees);
            float[] m = Identity();
            m[0] = c; m[2] = -s;
            m[8] = s; m[10] = c;
            return m;
        }

        /// <summary>Rotation about the Z (up) axis; positive = counter-clockwise seen from above.</summary>
        public static float[] RotationZ(float degrees)
        {
            (float s, float c) = SinCos(degrees);
            float[] m = Identity();
            m[0] = c; m[1] = s;
            m[4] = -s; m[5] = c;
            return m;
        }

        /// <summary>Matrix product a * b: applying the result is "b first, then a".</summary>
        public static float[] Multiply(float[] a, float[] b)
        {
            var r = new float[16];
            for (int col = 0; col < 4; col++)
                for (int row = 0; row < 4; row++)
                {
                    float sum = 0f;
                    for (int k = 0; k < 4; k++) sum += a[k * 4 + row] * b[col * 4 + k];
                    r[col * 4 + row] = sum;
                }
            return r;
        }

        private static (float sin, float cos) SinCos(float degrees)
        {
            float radians = degrees * MathF.PI / 180f;
            return (MathF.Sin(radians), MathF.Cos(radians));
        }
    }
}
