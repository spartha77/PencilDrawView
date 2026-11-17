// =============================================================================
// CadCore.GL / Scene.cpp
// =============================================================================
#include "CadCore/Scene.h"

#include <stdexcept>

namespace CadCore {

namespace {

/// Depth-first walk that accumulates world transforms, skipping hidden subtrees.
void AccumulateBounds(const Node* node, const Mat4& parentWorld, AABB& out) {
    if (!node->IsVisible()) return;
    Mat4 world = parentWorld * node->LocalTransform();
    out.Expand(node->LocalBounds().Transformed(world));
    for (const auto& child : node->Children()) AccumulateBounds(child.get(), world, out);
}

} // namespace

Scene::Scene() {
    m_root = std::make_unique<GroupNode>("Scene");
    m_root->SetId(kRootId);
    m_byId[kRootId] = m_root.get();
}

Node* Scene::Find(int id) const {
    auto it = m_byId.find(id);
    return it != m_byId.end() ? it->second : nullptr;
}

int Scene::Add(int parentId, std::unique_ptr<Node> node) {
    Node* parent = Find(parentId);
    if (!parent) throw std::invalid_argument("Parent node " + std::to_string(parentId) + " does not exist.");
    const int id = m_nextId++;
    node->SetId(id);
    m_byId[id] = parent->AddChild(std::move(node));
    return id;
}

void Scene::Unregister(Node* node) {
    m_byId.erase(node->Id());
    for (const auto& child : node->Children()) Unregister(child.get());
}

bool Scene::Remove(int id) {
    if (id == kRootId) return false;
    Node* node = Find(id);
    if (!node) return false;
    Unregister(node);                       // forget the IDs of the whole subtree
    node->Parent()->RemoveChild(node);      // returned unique_ptr dies here -> frees GPU buffers
    return true;
}

void Scene::Clear() {
    while (!m_root->Children().empty()) Remove(m_root->Children().back()->Id());
}

AABB Scene::VisibleBounds() const {
    AABB box;
    AccumulateBounds(m_root.get(), Mat4::Identity(), box);
    return box;
}

} // namespace CadCore
