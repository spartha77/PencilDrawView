// =============================================================================
// CadRenderer.WPF / MainWindow.xaml.cs
//
// Connects the controls of the window to the engine. Every handler follows the
// same pattern:   read the controls  ->  call CadViewport  ->  Redraw().
// The engine only draws when asked, so each change ends with Redraw().
// =============================================================================
using System;
using System.Collections.ObjectModel;
using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using CadBridge;
using Microsoft.Win32;

namespace CadRenderer.WPF {
   public partial class MainWindow : Window {
      /// <summary>Top-level rows of the scene tree (children hang off each item).</summary>
      private readonly ObservableCollection<SceneItem> _sceneItems = new ObservableCollection<SceneItem> ();

      /// <summary>
      /// The engine; null until the OpenGL window exists. Controls with an
      /// initial selection raise their "changed" events while the window is
      /// still being built - before ViewportHost itself exists - hence the "?.".
      /// </summary>
      private CadViewport? Vp => ViewportHost?.Viewport;

      private int _counter;   // numbers demo objects: "Box 1", "Box 2", ...

      public MainWindow () {
         InitializeComponent ();
         SceneTree.ItemsSource = _sceneItems;

         ViewportHost.ViewportReady += OnViewportReady;
         ViewportHost.NodeClicked += OnNodeClicked;
         ViewportHost.ViewportFailed += message => {
            StatusText.Text = "OpenGL error: " + message;
            MessageBox.Show (this, message, "OpenGL could not be started",
                            MessageBoxButton.OK, MessageBoxImage.Error);
         };
      }

      private void Redraw () => ViewportHost?.RequestRender ();

      // ---------------------------------------------------------------------
      // Start-up
      // ---------------------------------------------------------------------

      private void OnViewportReady (object? sender, EventArgs e) {
         if (Vp == null) return;

         // Push the initial state of every control into the engine.
         ApplyProjection ();
         ApplyDisplay ();
         ApplyGridAndAxes ();
         ApplyLighting ();
         ApplyPickTypes (null);

         BuildDemoScene ();
         Vp.FitAll ();
         Redraw ();

         // Shows which driver / GPU is in use, e.g. "OpenGL 4.6.0 NVIDIA ... | GLSL 4.60".
         StatusText.Text = "OpenGL " + Vp.GLVersion;
      }

      /// <summary>
      /// A small scene that exercises every node type:
      ///   Demo assembly (group)
      ///     +- Base plate   B-Rep style mesh with explicit edges, steel
      ///     +- Cover        B-Rep style mesh, transparent glass
      ///   Scan              point cloud with per-point colours
      /// </summary>
      private void BuildDemoScene () {
         if (Vp == null) return;

         SceneItem assembly = AddItem (null, Vp.AddGroup (CadViewport.RootId, "Demo assembly"),
                                      "Demo assembly", "Group");

         MeshData plate = DemoGeometry.Box (120f, 80f, 40f);
         int plateId = Vp.AddMesh (assembly.Id, "Base plate", true,
                                  plate.Positions, plate.Normals, plate.Indices, plate.EdgeSegments);
         Vp.ApplyMaterialPreset (plateId, MaterialPreset.Steel);
         AddItem (assembly, plateId, "Base plate", "B-Rep");

         MeshData cover = DemoGeometry.Box (60f, 100f, 60f);
         int coverId = Vp.AddMesh (assembly.Id, "Cover", true,
                                  cover.Positions, cover.Normals, cover.Indices, cover.EdgeSegments);
         Vp.ApplyMaterialPreset (coverId, MaterialPreset.Glass);
         Vp.SetNodeTransform (coverId, DemoGeometry.Translation (140f, -10f, 0f));
         AddItem (assembly, coverId, "Cover", "B-Rep");

         var (points, colors) = DemoGeometry.WavyPointCloud (120, 120, 1f, -150f, 120f, 20f);
         int cloudId = Vp.AddPointCloud (CadViewport.RootId, "Scan", points, colors, 3f);
         AddItem (null, cloudId, "Scan", "Points");
      }

      // ---------------------------------------------------------------------
      // Scene tree
      // ---------------------------------------------------------------------

      /// <summary>Adds a row to the tree (under `parent`, or at top level when null).</summary>
      private SceneItem AddItem (SceneItem? parent, int nodeId, string name, string kind) {
         var item = new SceneItem (nodeId, name, kind, OnItemVisibilityChanged);
         if (parent != null) parent.Add (item);
         else _sceneItems.Add (item);
         return item;
      }

      // ---------------------------------------------------------------------
      // Selection - the tree and the 3D view always show the same selection
      //   click a part in the view  -> its row is selected in the tree
      //   click a row in the tree   -> the part is highlighted in the view
      // Both routes end in OnTreeSelectionChanged, the single place that
      // tells the engine what to highlight.
      // ---------------------------------------------------------------------

      /// <summary>True while code (not the user) is changing the tree selection.</summary>
      private bool _syncingTree;

      /// <summary>
      /// The user clicked in the 3D view.
      ///   click          : select only what was clicked; empty space deselects all
      ///   Ctrl  + click  : add to the selection      (empty space: no change)
      ///   Shift + click  : remove from the selection (empty space: no change)
      /// </summary>
      private void OnNodeClicked (PickHit hit, ClickAction action) {
         if (Vp == null) return;

         if (hit.Blocked) {
            // The entity's type is filtered out: keep the current selection.
            StatusText.Text = $"This {hit.Type.ToString ().ToLowerInvariant ()} cannot be picked - its type is switched off.";
            return;
         }

         if (!hit.IsHit) {
            // Empty space (or nothing of a ticked type). Only a plain click
            // deselects; with Ctrl or Shift a stray click must not lose the
            // selection that was built up.
            if (action != ClickAction.Replace) return;
            Vp.ClearSelection ();
            SelectTreeRow (null);
            StatusText.Text = "Nothing selected.";
            Redraw ();
            return;
         }

         SceneItem? item = FindItem (_sceneItems, hit.NodeId);
         string what = Describe (hit, item);

         switch (action) {
            case ClickAction.Replace:
               Vp.Select (hit);
               SelectTreeRow (item);
               StatusText.Text = "Selected " + what + ".";
               break;

            case ClickAction.Add:
               bool added = Vp.AddToSelection (hit);
               SelectTreeRow (item);
               StatusText.Text = (added ? "Added " + what : "Already selected: " + what) + ". " + SelectionSummary ();
               break;

            case ClickAction.Remove:
               bool removed = Vp.RemoveFromSelection (hit);
               if (Vp.SelectionCount == 0) SelectTreeRow (null);
               StatusText.Text = (removed ? "Removed " + what : "Not selected: " + what) + ". " + SelectionSummary ();
               break;
         }
         Redraw ();
      }

      /// <summary>
      /// Selects a row of the scene tree from code (null = no row) WITHOUT
      /// letting OnTreeSelectionChanged replace the selection in the 3D view.
      /// The tree can show one row only; it follows the part clicked last.
      /// </summary>
      private void SelectTreeRow (SceneItem? item) {
         _syncingTree = true;
         try {
            if (item != null) item.IsSelected = true;
            else if (SceneTree.SelectedItem is SceneItem current) current.IsSelected = false;
         } finally { _syncingTree = false; }
      }

      /// <summary>"face 3 of Base plate at (60.000, 40.000, 40.000) mm"</summary>
      private static string Describe (PickHit hit, SceneItem? item) {
         string part = item?.Name ?? "part";
         if (hit.Type == PickType.Solid) return "solid " + part;
         return $"{hit.Type.ToString ().ToLowerInvariant ()} {hit.Index} of {part} at "
              + string.Format (CultureInfo.InvariantCulture, "({0:F3}, {1:F3}, {2:F3}) mm", hit.X, hit.Y, hit.Z);
      }

      private string SelectionSummary () {
         int n = Vp?.SelectionCount ?? 0;
         return n == 1 ? "1 entity selected." : n + " entities selected.";
      }

      /// <summary>A row of the scene tree was selected (or deselected) by the user.</summary>
      private void OnTreeSelectionChanged (object sender, RoutedPropertyChangedEventArgs<object> e) {
         if (_syncingTree) return;      // the 3D view made this change and has set the highlight

         var item = SceneTree.SelectedItem as SceneItem;
         Vp?.SetSelectedNode (item?.Id ?? -1);      // whole part; for a group, everything in it
         StatusText.Text = item != null ? $"Selected: {item.Name} ({item.Kind})" : "Nothing selected.";
         Redraw ();
      }

      // ---------------------------------------------------------------------
      // Pick types - the multi-select combo box
      //
      //   ticked types            -> CadViewport.PickTypes
      //   "(nothing)" ticked      -> every type is filtered out: nothing can be
      //                              picked and parts show the red pointer
      // ---------------------------------------------------------------------

      /// <summary>A check box in the combo was clicked.</summary>
      private void OnPickTypeToggled (object sender, RoutedEventArgs e) {
         ApplyPickTypes (sender as CheckBox);
      }

      /// <summary>
      /// A row was clicked beside its check box. A normal combo box would now
      /// "select" that row and close; here the click simply toggles the box.
      /// </summary>
      private void OnPickComboRowClicked (object sender, SelectionChangedEventArgs e) {
         if (PickTypesBox.SelectedItem is not CheckBox box) return;
         box.IsChecked = box.IsChecked != true;
         PickTypesBox.SelectedIndex = -1;          // no row stays selected
         ApplyPickTypes (box);
      }

      /// <summary>Reads the check boxes, updates the engine and the combo's summary text.</summary>
      private void ApplyPickTypes (CheckBox? changed) {
         // "(nothing)" and the four types exclude each other.
         if (changed == PickNothingCheck && PickNothingCheck.IsChecked == true) {
            PickVertexCheck.IsChecked = PickEdgeCheck.IsChecked = false;
            PickFaceCheck.IsChecked = PickSolidCheck.IsChecked = false;
         } else if (changed != null && changed != PickNothingCheck && changed.IsChecked == true) {
            PickNothingCheck.IsChecked = false;
         }

         PickType types = PickType.None;
         if (PickVertexCheck.IsChecked == true) types |= PickType.Vertex;
         if (PickEdgeCheck.IsChecked == true) types |= PickType.Edge;
         if (PickFaceCheck.IsChecked == true) types |= PickType.Face;
         if (PickSolidCheck.IsChecked == true) types |= PickType.Solid;

         // No type ticked at all means the same as "(nothing)".
         bool nothing = types == PickType.None;
         PickNothingCheck.IsChecked = nothing;

         if (Vp != null) {
            Vp.PickTypes = types;
            Vp.FilterOutPickTypes = nothing ? PickType.All : PickType.None;
         }

         // "Vertex, Edge" - the [Flags] enum formats itself as a comma list.
         string summary = nothing ? "(nothing)" : types == PickType.All ? "Vertex, Edge, Face, Solid" : types.ToString ();
         PickTypesBox.Text = summary;
         // The combo may overwrite its text while it finishes handling the
         // click, so set it once more when it is done.
         Dispatcher.BeginInvoke (new Action (() => PickTypesBox.Text = summary));
      }

      /// <summary>Finds the tree row of a scene node, searching all levels.</summary>
      private static SceneItem? FindItem (ObservableCollection<SceneItem> items, int nodeId) {
         foreach (SceneItem item in items) {
            if (item.Id == nodeId) return item;
            SceneItem? below = FindItem (item.Children, nodeId);
            if (below != null) return below;
         }
         return null;
      }

      /// <summary>A check box in the tree was toggled: set the node's "show" flag.</summary>
      private void OnItemVisibilityChanged (SceneItem item) {
         Vp?.SetNodeVisible (item.Id, item.IsVisible);
         Redraw ();
      }

      /// <summary>
      /// Where new objects go: into the selected group, next to the selected
      /// object, or at top level when nothing is selected.
      /// </summary>
      private SceneItem? TargetGroup () {
         var selected = SceneTree.SelectedItem as SceneItem;
         if (selected == null) return null;
         return selected.Kind == "Group" ? selected : selected.Parent;
      }

      private void OnAddGroup (object sender, RoutedEventArgs e) {
         if (Vp == null) return;
         SceneItem? parent = TargetGroup ();
         string name = "Group " + (++_counter);
         AddItem (parent, Vp.AddGroup (parent?.Id ?? CadViewport.RootId, name), name, "Group");
      }

      private void OnAddBox (object sender, RoutedEventArgs e) {
         if (Vp == null) return;
         SceneItem? parent = TargetGroup ();
         string name = "Box " + (++_counter);

         MeshData box = DemoGeometry.Box (50f, 50f, 50f);
         int id = Vp.AddMesh (parent?.Id ?? CadViewport.RootId, name, true,
                             box.Positions, box.Normals, box.Indices, box.EdgeSegments);
         // Place each new box a little further along -Y so they do not overlap.
         Vp.SetNodeTransform (id, DemoGeometry.Translation (_counter * 70f - 200f, -150f, 0f));
         Vp.SetNodeColor (id, 0.85f, 0.45f, 0.20f);
         AddItem (parent, id, name, "B-Rep");
         Redraw ();
      }

      private void OnAddPointCloud (object sender, RoutedEventArgs e) {
         if (Vp == null) return;
         SceneItem? parent = TargetGroup ();
         string name = "Cloud " + (++_counter);

         var (points, colors) = DemoGeometry.WavyPointCloud (80, 80, 1.5f, _counter * 40f - 250f, -350f, 30f);
         int id = Vp.AddPointCloud (parent?.Id ?? CadViewport.RootId, name, points, colors, 3f);
         AddItem (parent, id, name, "Points");
         Redraw ();
      }

      private void OnOpenStl (object sender, RoutedEventArgs e) {
         if (Vp == null) return;
         var dialog = new OpenFileDialog {
            Title = "Open STL file",
            Filter = "STL files (*.stl)|*.stl|All files (*.*)|*.*"
         };
         if (dialog.ShowDialog (this) != true) return;

         try {
            SceneItem? parent = TargetGroup ();
            int id = Vp.AddStlFile (parent?.Id ?? CadViewport.RootId, dialog.FileName);
            AddItem (parent, id, System.IO.Path.GetFileName (dialog.FileName), "STL");
            Vp.FitAll ();
            Redraw ();
            StatusText.Text = "Loaded " + dialog.FileName;
         } catch (Exception ex) {
            MessageBox.Show (this, ex.Message, "Could not load the STL file",
                            MessageBoxButton.OK, MessageBoxImage.Warning);
         }
      }

      private void OnRemoveSelected (object sender, RoutedEventArgs e) {
         if (Vp == null || SceneTree.SelectedItem is not SceneItem item) return;

         // Removing a node in the engine removes its whole subtree, so one
         // call is enough even for a group.
         Vp.RemoveNode (item.Id);
         if (item.Parent != null) item.Parent.Children.Remove (item);
         else _sceneItems.Remove (item);
         Redraw ();
      }

      /// <summary>
      /// Runs when a material is picked in the list (and for the Apply button).
      /// Applies it to every part that has something selected in the 3D view,
      /// or else to the row selected in the scene tree; for a group, to every
      /// mesh and point cloud beneath it.
      /// </summary>
      private void OnApplyMaterial (object sender, RoutedEventArgs e) {
         if (Vp == null) return;

         // Targets: every part that has something selected in the 3D view
         // (a selected face or edge counts for its whole part - a material
         // belongs to a part). Each part once, however many entities it has.
         var targets = new System.Collections.Generic.List<SceneItem> ();
         foreach (PickHit hit in Vp.GetSelection ()) {
            SceneItem? owner = FindItem (_sceneItems, hit.NodeId);
            if (owner != null && !targets.Contains (owner)) targets.Add (owner);
         }
         // Nothing selected in the view: fall back to the row selected in the tree.
         if (targets.Count == 0 && SceneTree.SelectedItem is SceneItem row) targets.Add (row);

         if (targets.Count == 0) {
            StatusText.Text = "Select a part first (click it in the view or in the scene tree), then pick the material.";
            return;
         }

         int changed = 0;
         foreach (SceneItem target in targets) changed += ApplyMaterialTo (target, MaterialBox.SelectedIndex);

         string materialName = (MaterialBox.SelectedItem as ComboBoxItem)?.Content as string ?? "Material";
         string where = targets.Count == 1 ? targets[0].Name : targets.Count + " parts";
         StatusText.Text = changed > 0
             ? $"{materialName} applied to {where}" + (changed > 1 ? $" ({changed} objects)." : ".")
             : $"{where} contains nothing a material can be applied to.";
         Redraw ();
      }

      /// <summary>Applies material `choice` to `item` and everything below it. Returns the number of objects changed.</summary>
      private int ApplyMaterialTo (SceneItem item, int choice) {
         if (Vp == null) return 0;

         int changed = 0;
         foreach (SceneItem child in item.Children) changed += ApplyMaterialTo (child, choice);
         if (item.Kind == "Group") return changed;

         bool ok;
         if (choice <= 3) {
            // Entries 0..3 are in the same order as the MaterialPreset enum.
            ok = Vp.ApplyMaterialPreset (item.Id, (MaterialPreset)choice);
         } else {
            // Coloured plastic: a diffuse colour with a weak, broad highlight.
            (float r, float g, float b) = choice switch {
               4 => (0.80f, 0.15f, 0.12f),   // red
               5 => (0.20f, 0.62f, 0.25f),   // green
               6 => (0.16f, 0.38f, 0.82f),   // blue
               _ => (0.92f, 0.52f, 0.12f)    // orange
            };
            ok = Vp.SetNodeMaterial (item.Id, r, g, b, 0.20f, 0.20f, 0.20f, 24f, 1f);
         }

         // Point clouds have no surface material; they take the colour only.
         if (!ok) ok = Vp.SetNodeColor (item.Id, MaterialColor (choice).r, MaterialColor (choice).g, MaterialColor (choice).b);
         return changed + (ok ? 1 : 0);
      }

      /// <summary>Base colour of each entry in the material list (used for point clouds).</summary>
      private static (float r, float g, float b) MaterialColor (int choice) => choice switch {
         0 => (0.42f, 0.46f, 0.52f),   // steel
         1 => (0.82f, 0.83f, 0.85f),   // aluminium
         2 => (0.78f, 0.62f, 0.25f),   // brass
         3 => (0.60f, 0.78f, 0.90f),   // glass
         4 => (0.80f, 0.15f, 0.12f),
         5 => (0.20f, 0.62f, 0.25f),
         6 => (0.16f, 0.38f, 0.82f),
         _ => (0.92f, 0.52f, 0.12f)
      };

      // ---------------------------------------------------------------------
      // View
      // ---------------------------------------------------------------------

      private void OnProjectionChanged (object sender, RoutedEventArgs e) {
         ApplyProjection ();
         Redraw ();
      }

      private void ApplyProjection () {
         // The combo box entries are in the same order as the ProjectionKind enum.
         Vp?.SetProjection ((ProjectionKind)ProjectionBox.SelectedIndex);
      }

      private void OnStandardView (object sender, RoutedEventArgs e) {
         // Each view button carries the name of a StandardViewKind value in its Tag.
         if (Vp == null || sender is not Button { Tag: string name }) return;
         if (!Enum.TryParse (name, out StandardViewKind view)) return;
         Vp.SetStandardView (view);
         Redraw ();
      }

      private void OnFitAll (object sender, RoutedEventArgs e) {
         Vp?.FitAll ();
         Redraw ();
      }

      // ---------------------------------------------------------------------
      // Display, grid, axes
      // ---------------------------------------------------------------------

      private void OnDisplayChanged (object sender, RoutedEventArgs e) {
         ApplyDisplay ();
         Redraw ();
      }

      private void ApplyDisplay () {
         if (Vp == null) return;
         Vp.SetDisplayMode ((DisplayKind)DisplayBox.SelectedIndex);
         Vp.SetEdgeWidth (BoldEdgesCheck.IsChecked == true ? 3.5f : 1.5f);
      }

      private void OnGridChanged (object sender, RoutedEventArgs e) {
         ApplyGridAndAxes ();
         Redraw ();
      }

      private void ApplyGridAndAxes () {
         if (Vp == null) return;
         float sizeMm = GridSizeBox.SelectedIndex == 1 ? 10000f : 1000f;
         Vp.SetGrid (GridCheck.IsChecked == true, sizeMm, 1f);
         // 6 px per model-mm: the 14 mm arrow is always about 84 px long.
         Vp.SetAxes (AxesCheck.IsChecked == true, 6f);
      }

      // ---------------------------------------------------------------------
      // Lighting
      // ---------------------------------------------------------------------

      private void OnLightingChanged (object sender, RoutedEventArgs e) {
         ApplyLighting ();
         Redraw ();
      }

      private void ApplyLighting () {
         if (Vp == null) return;

         bool ambientOnly = AmbientOnlyRadio.IsChecked == true;
         Vp.SetLightingMode (ambientOnly ? LightingKind.AmbientOnly : LightingKind.Lights);
         Vp.SetLightingTerms (AmbientTermCheck.IsChecked == true,
                             DiffuseTermCheck.IsChecked == true,
                             SpecularTermCheck.IsChecked == true);

         // The light fields only matter in the two-lights configuration.
         LightInputsPanel.IsEnabled = !ambientOnly;
         if (ambientOnly) return;

         Vp.SetLightCount (2);
         bool ok = ApplyLight (0, Light1TypeBox, Light1PosBox, Light1DirBox, Light1IntensityBox)
                 & ApplyLight (1, Light2TypeBox, Light2PosBox, Light2DirBox, Light2IntensityBox);
         if (ok) StatusText.Text = "Lights updated.";
      }

      /// <summary>Reads one light's fields and sends them to the engine. False on a bad entry.</summary>
      private bool ApplyLight (int index, ComboBox typeBox, TextBox posBox, TextBox dirBox, TextBox intensityBox) {
         if (Vp == null) return false;

         if (!TryParseVector (posBox.Text, out float px, out float py, out float pz))
            return ReportBadLightInput (index, "position (expected: x, y, z)");
         if (!TryParseVector (dirBox.Text, out float dx, out float dy, out float dz))
            return ReportBadLightInput (index, "direction (expected: x, y, z)");
         if (dx == 0f && dy == 0f && dz == 0f)
            return ReportBadLightInput (index, "direction (must not be 0, 0, 0)");
         if (!float.TryParse (intensityBox.Text, NumberStyles.Float, CultureInfo.InvariantCulture, out float intensity)
             || intensity < 0f)
            return ReportBadLightInput (index, "intensity (expected a number such as 0.8)");

         // The combo box entries are in the same order as the LightKind enum.
         Vp.SetLight (index, (LightKind)typeBox.SelectedIndex, px, py, pz, dx, dy, dz, 1f, 1f, 1f, intensity);
         return true;
      }

      private bool ReportBadLightInput (int index, string what) {
         StatusText.Text = $"Light {index + 1}: check the {what}. The previous value is still in use.";
         return false;
      }

      /// <summary>Parses "x, y, z" (commas, semicolons or spaces; decimal point).</summary>
      private static bool TryParseVector (string text, out float x, out float y, out float z) {
         x = y = z = 0f;
         string[] parts = text.Split (new[] { ',', ';', ' ' }, StringSplitOptions.RemoveEmptyEntries);
         return parts.Length == 3
             && float.TryParse (parts[0], NumberStyles.Float, CultureInfo.InvariantCulture, out x)
             && float.TryParse (parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out y)
             && float.TryParse (parts[2], NumberStyles.Float, CultureInfo.InvariantCulture, out z);
      }
   }
}
