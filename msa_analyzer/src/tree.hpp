#ifndef tree_hpp_t
#define tree_hpp_t

#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

class source_position_t
{
  public:
    source_position_t(const std::filesystem::path     &file,
                      const std::pair<size_t, size_t> &start_position,
                      const std::pair<size_t, size_t> &end_position)
        : file(file)
        , start_position(start_position)
        , end_position(end_position)
    { }

    std::filesystem::path     get_file() const;
    std::pair<size_t, size_t> get_start_position() const;
    size_t                    get_start_line() const;
    size_t                    get_start_column() const;
    std::pair<size_t, size_t> get_end_position() const;
    size_t                    get_end_line() const;
    size_t                    get_end_column() const;
    bool                      operator< (const source_position_t &other) const;
    bool                      operator== (const source_position_t &other) const;
    std::string               render() const;
  private:
    std::filesystem::path     file;
    std::pair<size_t, size_t> start_position;
    std::pair<size_t, size_t> end_position;
};

class node_t
{
  public:
    node_t(const std::string       &tag,
           const std::string       &ts_text,
           const std::string       &ts_type,
           const bool              &ts_is_named,
           const source_position_t &source_position);

    const std::string       &get_tag() const;
    const std::string       &get_ts_text() const;
    void                     add_child(node_t *child);
    void                     render(const int &whitespace) const;
    bool                     is_leaf() const;
    std::vector<node_t *>    get_pointer_to_every_node();
    void                     calculate_subtree_hashes();
    const std::size_t       &get_subtree_hash() const;
    const int               &get_connected_leaf_weight();
    bool                     is_ancestor_of(node_t *node);
    std::vector<node_t *>    get_ancestors();
    const source_position_t &get_source_position() const;
    const std::vector<std::unique_ptr<node_t>> &get_children();
    node_t                *get_child_by_tag(const std::string &tag);
    node_t                *get_parent();
    node_t                *get_root();
    std::vector<node_t *> &get_leafs();
    bool                   get_is_in_intersection();
    void                   set_is_in_intersection();
    std::vector<node_t *>  subtrees_not_in_intersection();

    std::string feature {};

    enum class relative_position_t
    {
      before,
      after,
      overlapping,
    };

    relative_position_t get_relative_position(node_t *other);

    void set_node_types(const std::vector<std::string> &types);
    const std::vector<std::string> &get_node_types() const;
    size_t                          weight {};
    void
        set_feature_a_ffiliations(const std::set<size_t> &feature_affiliations);
    std::set<size_t> get_feature_affiliations();
    std::string      get_node_rep();
  private:
    node_t                              *parent { nullptr };
    std::vector<std::unique_ptr<node_t>> children {};
    std::string                          tag;
    std::string                          ts_text;
    std::string                          ts_type;
    bool                                 ts_is_named;
    int                                  connected_leaf_weight {};
    std::size_t                          subtree_hash {};
    source_position_t                    source_position;
    std::vector<std::string>             all_types {};
    std::vector<node_t *>                connected_leaves {};
    bool                                 is_in_intersection { false };
    std::set<size_t>                     feature_affiliations {};

    void set_parent(node_t *parent);
};

#endif // TREE_HPP
