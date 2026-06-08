#include "tree.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
#include <stack>

std::filesystem::path node_position::get_file() const
{
  return file;
}

std::pair<size_t, size_t> node_position::get_start_position() const
{
  return start_position;
}

size_t node_position::get_start_line() const
{
  return start_position.first;
}

size_t node_position::get_start_column() const
{
  return start_position.second;
}

std::pair<size_t, size_t> node_position::get_end_position() const
{
  return end_position;
}

size_t node_position::get_end_line() const
{
  return end_position.first;
}

size_t node_position::get_end_column() const
{
  return end_position.second;
}

bool node_position::operator< (const node_position &other) const
{
  return start_position < other.start_position;
}

bool node_position::operator== (const node_position &other) const
{
  return start_position == other.start_position
         && end_position == other.end_position && file == other.file;
}

std::string node_position::render() const
{
  return file.string() + "/" + std::to_string(start_position.first) + ":"
         + std::to_string(start_position.second) + "-"
         + std::to_string(end_position.first) + ":"
         + std::to_string(end_position.second);
}

node_t::node_t(const std::string   &tag,
               const std::string   &ts_text,
               const std::string   &ts_type,
               const bool          &ts_is_named,
               const node_position &m_node_position_p)
    : tag(tag)
    , ts_text(ts_text)
    , ts_type(ts_type)
    , ts_is_named(ts_is_named)
    , m_node_position(m_node_position_p)
{ }

const std::string &node_t::get_tag() const
{
  return tag;
}

const std::string &node_t::get_ts_text() const
{
  return ts_text;
}

const int &node_t::get_connected_leaf_weight()
{
  if (connected_leaf_weight == 0)
  {
    if (is_leaf())
    {
      if (tag == "identifier")
      {
        connected_leaf_weight = 3;
      }
      else if (ts_is_named)
      {
        connected_leaf_weight = 2;
      }
      else
      {
        connected_leaf_weight = 1;
      }
    }
    else
    {
      for (auto &child : children)
      {
        connected_leaf_weight += child->get_connected_leaf_weight();
      }
    }
  }
  return connected_leaf_weight;
}

void node_t::add_child(std::shared_ptr<node_t> child)
{
  children.push_back(child);
  child->set_parent(weak_from_this());
}

void node_t::set_parent(std::weak_ptr<node_t> parent)
{
  this->parent = parent;
}

void node_t::render(const int &whitespace) const
{
  for (int i = 0; i < whitespace; i++)
  {
    std::cout << " ";
  }
  std::cout << tag;
  if (is_leaf())
  {
    std::cout << ": \"" << ts_text << "\"";
  }
  std::cout << std::endl;
  for (auto &child : children)
  {
    child->render(whitespace + 2);
  }
}

bool node_t::is_leaf() const
{
  return children.empty();
}

std::vector<std::shared_ptr<node_t>> node_t::get_pointer_to_every_node()
{
  std::vector<std::shared_ptr<node_t>> nodes;
  std::stack<std::shared_ptr<node_t>>  stack;
  stack.push(shared_from_this());

  while (!stack.empty())
  {
    auto current = stack.top();
    stack.pop();
    nodes.push_back(current);

    for (auto it = current->children.rbegin(); it != current->children.rend();
         ++it)
    {
      stack.push(*it);
    }
  }

  return nodes;
}

void node_t::calculate_subtree_hashes()
{
  if (subtree_hash == 0)
  {
    std::string temp_hash;
    temp_hash = tag;
    if (is_leaf())
    {
      temp_hash += ts_text;
    }
    for (auto &child : children)
    {
      child->calculate_subtree_hashes();
      temp_hash.append(std::to_string(child->get_subtree_hash()));
    }
    subtree_hash = std::hash<std::string> {}(temp_hash);
    if (subtree_hash == 0) // for the very rare case that the hash is 0
    {
      subtree_hash = 1;
    }
  }
}

std::shared_ptr<node_t> node_t::get_child_by_tag(const std::string &tag)
{
  for (const auto &child : children)
  {
    if (child->get_tag() == tag)
    {
      return child;
    }
  }
  return nullptr;
}

std::shared_ptr<node_t> node_t::get_parent()
{
  return parent.lock();
}

std::shared_ptr<node_t> node_t::get_root()
{
  if (parent.expired())
  {
    return shared_from_this();
  }
  return parent.lock()->get_root();
}

bool node_t::is_ancestor_of(std::shared_ptr<node_t> node)
{
  auto current = node;
  while (!current->parent.expired())
  {
    if (current->parent.lock().get() == this)
    {
      return true;
    }
    else
    {
      current = current->parent.lock();
    }
  }
  return false;
}

const std::vector<std::shared_ptr<node_t>> &node_t::get_children()
{
  return children;
}

node_t::relative_position
    node_t::get_relative_position(std::shared_ptr<node_t> other)
{
  if (other.get() == this)
  {
    return relative_position::overlapping;
  }
  if (is_ancestor_of(other) || other->is_ancestor_of(shared_from_this()))
  {
    return relative_position::overlapping;
  }
  else
  {
    int compareValue1, compareValue2;
    if (m_node_position.get_start_position().first
        == other->m_node_position.get_start_position().first)
    {
      compareValue1 = m_node_position.get_start_position().second;
      compareValue2 = other->m_node_position.get_start_position().second;
    }
    else
    {
      compareValue1 = this->m_node_position.get_start_position().first;
      compareValue2 = other->m_node_position.get_start_position().first;
    }

    if (compareValue1 < compareValue2)
    {
      return relative_position::before;
    }
    else
    {
      return relative_position::after;
    }
  }
}

const node_position &node_t::get_node_position() const
{
  return m_node_position;
}

void node_t::set_node_types(const std::vector<std::string> &types)
{
  all_types = types;
}

const std::vector<std::string> &node_t::get_node_types() const
{
  return all_types;
}

std::vector<std::shared_ptr<node_t>> node_t::get_leaves()
{
  if (connected_leaves.empty())
  {
    if (this->is_leaf())
    {
      connected_leaves.push_back(shared_from_this());
    }
    else
    {
      for (auto &child : this->children)
      {
        auto childResult { child->get_leaves() };
        connected_leaves.insert(
            connected_leaves.end(), childResult.begin(), childResult.end());
      }
    }
  }

  std::vector<std::shared_ptr<node_t>> connectedLeavesShared;
  for (auto l : connected_leaves)
  {
    connectedLeavesShared.push_back(l.lock());
  }
  return connectedLeavesShared;
}

std::vector<std::shared_ptr<node_t>> node_t::get_ancestors()
{
  std::vector<std::shared_ptr<node_t>> ancestors;
  std::shared_ptr<node_t>              current = shared_from_this();
  while (!current->parent.expired())
  {
    ancestors.push_back(current->parent.lock());
    current = current->parent.lock();
  }
  return ancestors;
}

bool node_t::get_is_in_intersection()
{
  if (is_in_intersection)
  {
    return true;
  }
  for (auto ancestor : this->get_ancestors())
  {
    if (ancestor->get_is_in_intersection())
    {
      return true;
    }
  }
  return false;
}

void node_t::set_is_in_intersection()
{
  is_in_intersection = true;
  for (auto &child : children)
  {
    child->set_is_in_intersection();
  }
}

std::vector<std::shared_ptr<node_t>> node_t::subtrees_not_in_intersection()
{
  std::vector<std::shared_ptr<node_t>> result;
  std::vector<std::shared_ptr<node_t>> descendants {
    this->get_pointer_to_every_node()
  };
  if (std::none_of(descendants.begin(),
                   descendants.end(),
                   [](std::shared_ptr<node_t> &node)
                   { return node->get_is_in_intersection(); }))
  {
    result.push_back(shared_from_this());
    return result;
  }
  for (auto &child : children)
  {
    auto childResult { child->subtrees_not_in_intersection() };
    result.insert(result.end(), childResult.begin(), childResult.end());
  }
  return result;
}

void node_t::set_feature_affiliations(
    const std::set<size_t> &feature_affiliations)
{
  this->feature_affiliations = feature_affiliations;
}

std::set<size_t> node_t::get_feature_affiliations()
{
  return this->feature_affiliations;
}

std::string node_t::get_node_rep()
{
  if (this->is_leaf())
  {
    return this->get_ts_text();
  }
  else
  {
    return this->get_tag();
  }
}
