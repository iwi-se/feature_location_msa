#include "tree.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
#include <stack>

std::filesystem::path SourcePosition::getFile() const
{
  return file;
}

std::pair<size_t, size_t> SourcePosition::getStartPosition() const
{
  return startPosition;
}

size_t SourcePosition::getStartLine() const
{
  return startPosition.first;
}

size_t SourcePosition::getStartColumn() const
{
  return startPosition.second;
}

std::pair<size_t, size_t> SourcePosition::getEndPosition() const
{
  return endPosition;
}

size_t SourcePosition::getEndLine() const
{
  return endPosition.first;
}

size_t SourcePosition::getEndColumn() const
{
  return endPosition.second;
}

bool SourcePosition::operator< (const SourcePosition &other) const
{
  return startPosition < other.startPosition;
}

bool SourcePosition::operator== (const SourcePosition &other) const
{
  return startPosition == other.startPosition
         && endPosition == other.endPosition && file == other.file;
}

std::string SourcePosition::render() const
{
  return file.string() + "/" + std::to_string(startPosition.first) + ":"
         + std::to_string(startPosition.second) + "-"
         + std::to_string(endPosition.first) + ":"
         + std::to_string(endPosition.second);
}

Node::Node(const std::string    &tag,
           const std::string    &tsText,
           const std::string    &tsType,
           const bool           &tsIsNamed,
           const SourcePosition &sourcePosition)
    : tag(tag)
    , tsText(tsText)
    , tsType(tsType)
    , tsIsNamed(tsIsNamed)
    , sourcePosition(sourcePosition)
{ }

const std::string &Node::getTag() const
{
  return tag;
}

const std::string &Node::getTsText() const
{
  return tsText;
}

const int &Node::getConnectedLeafWeight()
{
  if (connectedLeafWeight == 0)
  {
    if (isLeaf())
    {
      if (tag == "identifier")
      {
        connectedLeafWeight = 3;
      }
      else if (tsIsNamed)
      {
        connectedLeafWeight = 2;
      }
      else
      {
        connectedLeafWeight = 1;
      }
    }
    else
    {
      for (auto &child : children)
      {
        connectedLeafWeight += child->getConnectedLeafWeight();
      }
    }
  }
  return connectedLeafWeight;
}

void Node::addChild(std::shared_ptr<Node> child)
{
  children.push_back(child);
  child->setParent(weak_from_this());
}

void Node::setParent(std::weak_ptr<Node> parent)
{
  this->parent = parent;
}

void Node::render(const int &whitespace) const
{
  for (int i = 0; i < whitespace; i++)
  {
    std::cout << " ";
  }
  std::cout << tag;
  if (isLeaf())
  {
    std::cout << ": \"" << tsText << "\"";
  }
  std::cout << std::endl;
  for (auto &child : children)
  {
    child->render(whitespace + 2);
  }
}

bool Node::isLeaf() const
{
  return children.empty();
}

std::vector<std::shared_ptr<Node>> Node::getPointerToEveryNode()
{
  std::vector<std::shared_ptr<Node>> nodes;
  std::stack<std::shared_ptr<Node>>  stack;
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

void Node::calculateSubtreeHashes()
{
  if (subtreeHash == 0)
  {
    std::string temp_hash;
    temp_hash = tag;
    if (isLeaf())
    {
      temp_hash += tsText;
    }
    for (auto &child : children)
    {
      child->calculateSubtreeHashes();
      temp_hash.append(std::to_string(child->getSubtreeHash()));
    }
    subtreeHash = std::hash<std::string> {}(temp_hash);
    if (subtreeHash == 0) // for the very rare case that the hash is 0
    {
      subtreeHash = 1;
    }
  }
}

const std::size_t &Node::getSubtreeHash() const
{
  return subtreeHash;
}

std::shared_ptr<Node> Node::getChildByTag(const std::string &tag)
{
  for (const auto &child : children)
  {
    if (child->getTag() == tag)
    {
      return child;
    }
  }
  return nullptr;
}

std::shared_ptr<Node> Node::getParent()
{
  return parent.lock();
}

std::shared_ptr<Node> Node::getRoot()
{
  if (parent.expired())
  {
    return shared_from_this();
  }
  return parent.lock()->getRoot();
}

bool Node::isAncestorOf(std::shared_ptr<Node> node)
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

const std::vector<std::shared_ptr<Node>> &Node::getChildren()
{
  return children;
}

Node::RelativePosition Node::getRelativePosition(std::shared_ptr<Node> other)
{
  if (other.get() == this)
  {
    return RelativePosition::overlapping;
  }
  if (isAncestorOf(other) || other->isAncestorOf(shared_from_this()))
  {
    return RelativePosition::overlapping;
  }
  else
  {
    int compareValue1, compareValue2;
    if (sourcePosition.getStartPosition().first
        == other->sourcePosition.getStartPosition().first)
    {
      compareValue1 = sourcePosition.getStartPosition().second;
      compareValue2 = other->sourcePosition.getStartPosition().second;
    }
    else
    {
      compareValue1 = this->sourcePosition.getStartPosition().first;
      compareValue2 = other->sourcePosition.getStartPosition().first;
    }

    if (compareValue1 < compareValue2)
    {
      return RelativePosition::before;
    }
    else
    {
      return RelativePosition::after;
    }
  }
}

const SourcePosition &Node::getSourcePosition() const
{
  return sourcePosition;
}

void Node::setNodeTypes(const std::vector<std::string> &types)
{
  allTypes = types;
}

const std::vector<std::string> &Node::getNodeTypes() const
{
  return allTypes;
}

std::vector<std::shared_ptr<Node>> Node::getLeafs()
{
  if (connectedLeaves.empty())
  {
    if (this->isLeaf())
    {
      connectedLeaves.push_back(shared_from_this());
    }
    else
    {
      for (auto &child : this->children)
      {
        auto childResult { child->getLeafs() };
        connectedLeaves.insert(
            connectedLeaves.end(), childResult.begin(), childResult.end());
      }
    }
  }

  std::vector<std::shared_ptr<Node>> connectedLeavesShared;
  for (auto l : connectedLeaves)
  {
    connectedLeavesShared.push_back(l.lock());
  }
  return connectedLeavesShared;
}

std::vector<std::shared_ptr<Node>> Node::getAncestors()
{
  std::vector<std::shared_ptr<Node>> ancestors;
  std::shared_ptr<Node>              current = shared_from_this();
  while (!current->parent.expired())
  {
    ancestors.push_back(current->parent.lock());
    current = current->parent.lock();
  }
  return ancestors;
}

bool Node::getIsInIntersection()
{
  if (isInIntersection)
  {
    return true;
  }
  for (auto ancestor : this->getAncestors())
  {
    if (ancestor->getIsInIntersection())
    {
      return true;
    }
  }
  return false;
}

void Node::setIsInIntersection()
{
  isInIntersection = true;
  for (auto &child : children)
  {
    child->setIsInIntersection();
  }
}

std::vector<std::shared_ptr<Node>> Node::subtreesNotInIntersection()
{
  std::vector<std::shared_ptr<Node>> result;
  std::vector<std::shared_ptr<Node>> descendants {
    this->getPointerToEveryNode()
  };
  if (std::none_of(descendants.begin(),
                   descendants.end(),
                   [](std::shared_ptr<Node> &node)
                   { return node->getIsInIntersection(); }))
  {
    result.push_back(shared_from_this());
    return result;
  }
  for (auto &child : children)
  {
    auto childResult { child->subtreesNotInIntersection() };
    result.insert(result.end(), childResult.begin(), childResult.end());
  }
  return result;
}

void Node::setFeatureAFfiliations(const std::set<size_t> &featureAffiliations)
{
  this->featureAffiliations = featureAffiliations;
}

std::set<size_t> Node::getFeatureAffiliations()
{
  return this->featureAffiliations;
}

std::string Node::getNodeRep()
{
  if (this->isLeaf())
  {
    return this->getTsText();
  }
  else
  {
    return this->getTag();
  }
}
