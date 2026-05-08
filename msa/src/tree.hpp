#pragma once
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

class SourcePosition
{
  public:
    SourcePosition(const std::filesystem::path     &file,
                   const std::pair<size_t, size_t> &startPosition,
                   const std::pair<size_t, size_t> &endPosition)
        : file(file)
        , startPosition(startPosition)
        , endPosition(endPosition)
    { }

    std::filesystem::path     getFile() const;
    std::pair<size_t, size_t> getStartPosition() const;
    size_t                    getStartLine() const;
    size_t                    getStartColumn() const;
    std::pair<size_t, size_t> getEndPosition() const;
    size_t                    getEndLine() const;
    size_t                    getEndColumn() const;
    bool                      operator< (const SourcePosition &other) const;
    bool                      operator== (const SourcePosition &other) const;
    std::string               render() const;
  private:
    std::filesystem::path     file;
    std::pair<size_t, size_t> startPosition;
    std::pair<size_t, size_t> endPosition;
};

class Node: public std::enable_shared_from_this<Node>
{
  public:
    Node(const std::string    &tag,
         const std::string    &tsText,
         const std::string    &tsType,
         const bool           &tsIsNamed,
         const SourcePosition &sourcePosition);

    const std::string                 &getTag() const;
    const std::string                 &getTsText() const;
    void                               addChild(std::shared_ptr<Node> child);
    void                               render(const int &whitespace) const;
    bool                               isLeaf() const;
    std::vector<std::shared_ptr<Node>> getPointerToEveryNode();
    void                               calculateSubtreeHashes();
    const std::size_t                 &getSubtreeHash() const;
    const int                         &getConnectedLeafWeight();
    bool                               isAncestorOf(std::shared_ptr<Node> node);
    std::vector<std::shared_ptr<Node>> getAncestors();
    const SourcePosition              &getSourcePosition() const;
    const std::vector<std::shared_ptr<Node>> &getChildren();
    std::shared_ptr<Node>              getChildByTag(const std::string &tag);
    std::shared_ptr<Node>              getParent();
    std::shared_ptr<Node>              getRoot();
    std::vector<std::shared_ptr<Node>> getLeafs();
    bool                               getIsInIntersection();
    void                               setIsInIntersection();
    std::vector<std::shared_ptr<Node>> subtreesNotInIntersection();

    enum class RelativePosition
    {
      before,
      after,
      overlapping,
    };

    RelativePosition getRelativePosition(std::shared_ptr<Node> other);

    void setNodeTypes(const std::vector<std::string> &types);
    const std::vector<std::string> &getNodeTypes() const;
    size_t                          weight {};
    void setFeatureAFfiliations(const std::set<size_t> &featureAffiliations);
    std::set<size_t> getFeatureAffiliations();
    std::string      getNodeRep();
  private:
    std::weak_ptr<Node>                parent {};
    std::vector<std::shared_ptr<Node>> children {};
    std::string                        tag;
    std::string                        tsText;
    std::string                        tsType;
    bool                               tsIsNamed;
    int                                connectedLeafWeight {};
    std::size_t                        subtreeHash {};
    SourcePosition                     sourcePosition;
    std::vector<std::string>           allTypes {};
    std::vector<std::weak_ptr<Node>>   connectedLeaves {};
    bool                               isInIntersection { false };
    std::set<size_t>                   featureAffiliations {};

    void setParent(std::weak_ptr<Node> parent);
};
