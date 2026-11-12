// =============================================================================
// CadRenderer.WPF / SceneItem.cs
//
// One row of the scene tree shown in the UI. It mirrors a node of the native
// scene graph: `Id` is the node's ID in the engine, and ticking / unticking the
// row's check box sets that node's "show" flag.
// =============================================================================
using System;
using System.Collections.ObjectModel;
using System.ComponentModel;

namespace CadRenderer.WPF {
   public sealed class SceneItem : INotifyPropertyChanged {
      private bool _isVisible = true;
      private readonly Action<SceneItem> _visibilityChanged;

      public SceneItem (int id, string name, string kind, Action<SceneItem> visibilityChanged) {
         Id = id;
         Name = name;
         Kind = kind;
         _visibilityChanged = visibilityChanged;
      }

      /// <summary>Node ID in the native scene graph.</summary>
      public int Id { get; }
      public string Name { get; }
      /// <summary>"Group", "B-Rep", "STL", "Points" - shown next to the name.</summary>
      public string Kind { get; }

      public SceneItem? Parent { get; private set; }
      public ObservableCollection<SceneItem> Children { get; } = new ObservableCollection<SceneItem> ();

      private bool _isSelected;

      /// <summary>
      /// Bound (two-way) to the tree row's IsSelected. Setting it from code
      /// selects the row - that is how a click in the 3D view selects the tree.
      /// </summary>
      public bool IsSelected {
         get => _isSelected;
         set {
            if (_isSelected == value) return;
            _isSelected = value;
            PropertyChanged?.Invoke (this, new PropertyChangedEventArgs (nameof (IsSelected)));
         }
      }

      /// <summary>Bound to the check box. Hiding a group hides its whole subtree.</summary>
      public bool IsVisible {
         get => _isVisible;
         set {
            if (_isVisible == value) return;
            _isVisible = value;
            PropertyChanged?.Invoke (this, new PropertyChangedEventArgs (nameof (IsVisible)));
            _visibilityChanged (this);
         }
      }

      public void Add (SceneItem child) {
         child.Parent = this;
         Children.Add (child);
      }

      public event PropertyChangedEventHandler? PropertyChanged;
   }
}
