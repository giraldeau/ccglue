// generator.hpp
// Copyright (c) 2005-2011 Ben Hanson (http://www.benhanson.net/)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file licence_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#ifndef LEXERTL_GENERATOR_HPP
#define LEXERTL_GENERATOR_HPP

#include <algorithm>
#include "bool.hpp"
#include "partition/charset.hpp"
#include "char_traits.hpp"
#include "partition/equivset.hpp"
#include <memory>
#include "parser/parser.hpp"
#include "containers/ptr_list.hpp"
#include "rules.hpp"
#include "size_t.hpp"
#include "state_machine.hpp"

namespace lexertl
{
template<typename rules, typename sm, typename char_traits = basic_char_traits
    <typename sm::traits::input_char_type> >
class basic_generator
{
public:
    typedef typename rules::id_type id_type;
    typedef typename rules::char_type rules_char_type;
    typedef typename sm::traits sm_traits;
    typedef detail::basic_parser<rules_char_type, sm_traits> parser;
    typedef typename parser::charset_map charset_map;
    typedef typename parser::node node;
    typedef typename parser::node_ptr_vector node_ptr_vector;

    static void build (const rules &rules_, sm &sm_)
    {
        const std::size_t size_ = rules_.statemap ().size ();
        // Strong exception guarantee
        // http://www.boost.org/community/exception_safety.html
        internals internals_;
        sm temp_sm_;
        node_ptr_vector node_ptr_vector_;

        internals_._eoi = rules_.eoi ();
        internals_.add_states (size_);

        for (id_type index_ = 0; index_ < size_; ++index_)
        {
            if (!rules_.regexes ()[index_].empty ())
            {
                // Note that the following variables are per DFA.
                // Map of regex charset tokens (strings) to index
                charset_map charset_map_;
                // Used to fix up $ and \n clashes.
                id_type eol_id_ = sm_traits::npos ();
                // Regex syntax tree
                node *root_ = build_tree (rules_, index_, node_ptr_vector_,
                    charset_map_, eol_id_);

                build_dfa (charset_map_, root_, internals_, temp_sm_, index_,
                    eol_id_);

                if (internals_._dfa[index_]->size () /
                    internals_._dfa_alphabet[index_] >= sm_traits::npos ())
                {
                    // Overflow
                    throw runtime_error ("The data type you have chosen "
                        "cannot hold this many DFA rows.");
                }
            }
        }

        // If you get a compile error here the id_type from rules and
        // state machine do no match.
        create (internals_, temp_sm_, rules_.features (), lookup ());
        sm_.swap (temp_sm_);
    }

    static node *build_tree (const rules &rules_, const std::size_t dfa_,
        node_ptr_vector &node_ptr_vector_, charset_map &charset_map_,
        id_type &eol_id_)
    {
        typename parser::macro_map macro_map_;
        parser parser_ (rules_.locale (), node_ptr_vector_, macro_map_,
            charset_map_, rules_.eoi ());
        const typename rules::string_deque_deque &regexes_ =
            rules_.regexes ();
        typename rules::string_deque::const_iterator regex_iter_ =
            regexes_[dfa_].begin ();
        typename rules::string_deque::const_iterator regex_iter_end_ =
            regexes_[dfa_].end ();
        const typename rules::string &regex_ = *regex_iter_;
        const typename rules::id_vector_deque &ids_ = rules_.ids ();
        const typename rules::id_vector_deque &user_ids_ =
            rules_.user_ids ();
        typename rules::id_vector::const_iterator id_iter_ =
            ids_[dfa_].begin ();
        typename rules::id_vector::const_iterator user_id_iter_ =
            user_ids_[dfa_].begin ();
        const typename rules::id_vector_deque &next_dfas_ =
            rules_.next_dfas ();
        const typename rules::id_vector_deque &pushes_ = rules_.pushes ();
        const typename rules::bool_vector_deque &pops_ = rules_.pops ();
        typename rules::id_vector::const_iterator next_dfa_iter_ =
            next_dfas_[dfa_].begin ();
        typename rules::id_vector::const_iterator push_dfa_iter_ =
            pushes_[dfa_].begin ();
        typename rules::bool_vector::const_iterator pop_dfa_iter_ =
            pops_[dfa_].begin ();
        const bool seen_bol_ = (rules_.features ()[dfa_] & bol_bit) != 0;
        node *root_ = 0;

        // Macros have a different context per lexer state
        // as equivsets (generally) differ.
        build_macros (rules_, macro_map_, node_ptr_vector_, charset_map_,
            eol_id_);
        root_ = parser_.parse (regex_.c_str (),
            regex_.c_str () + regex_.size (), *id_iter_, *user_id_iter_,
            *next_dfa_iter_, *push_dfa_iter_, *pop_dfa_iter_,
            rules_.flags (), eol_id_, seen_bol_, false);
        ++regex_iter_;
        ++id_iter_;
        ++user_id_iter_;
        ++next_dfa_iter_;
        ++push_dfa_iter_;
        ++pop_dfa_iter_;

        // Build syntax trees
        while (regex_iter_ != regex_iter_end_)
        {
            // Re-declare var, otherwise we perform an assignment..!
            const typename rules::string &regex_ = *regex_iter_;
            node *rhs_ = parser_.parse (regex_.c_str (),
                regex_.c_str () + regex_.size (), *id_iter_, *user_id_iter_,
                *next_dfa_iter_, *push_dfa_iter_, *pop_dfa_iter_,
                rules_.flags (), eol_id_,
                (rules_.features ()[dfa_] & bol_bit) != 0, false);

            node_ptr_vector_->push_back
                (static_cast<selection_node *>(0));
            node_ptr_vector_->back () = new selection_node (root_, rhs_);
            root_ = node_ptr_vector_->back ();

            ++regex_iter_;
            ++id_iter_;
            ++user_id_iter_;
            ++next_dfa_iter_;
            ++push_dfa_iter_;
            ++pop_dfa_iter_;
        }

        return root_;
    }

protected:
    typedef bool_<sm_traits::compressed> compressed;
    typedef detail::basic_equivset<id_type> equivset;
    typedef detail::ptr_list<equivset> equivset_list;
    typedef std::auto_ptr<equivset> equivset_ptr;
    typedef typename sm_traits::char_type sm_char_type;
    typedef detail::basic_charset<sm_char_type, id_type> charset;
    typedef std::auto_ptr<charset> charset_ptr;
    typedef detail::ptr_list<charset> charset_list;
    typedef detail::basic_internals<id_type> internals;
    typedef typename internals::id_type_vector id_type_vector;
    typedef typename charset::index_set index_set;
    typedef std::vector<index_set> index_set_vector;
    typedef bool_<sm_traits::is_dfa> is_dfa;
    typedef bool_<sm_traits::lookup> lookup;
    typedef typename parser::macro_map macro_map;
    typedef typename macro_map::iterator macro_iter;
    typedef std::pair<macro_iter, bool> macro_iter_pair;
    typedef std::set<const node *> node_set;
    typedef detail::ptr_vector<node_set> node_set_vector;
    typedef typename node::node_vector node_vector;
    typedef detail::ptr_vector<node_vector> node_vector_vector;
    typedef std::pair<typename rules::string, const node *> macro_pair;
    typedef typename parser::selection_node selection_node;
    typedef typename std::map<id_type, id_type> id_type_map;
    typedef typename std::pair<id_type, id_type> id_type_pair;
    typedef typename std::vector<std::size_t> size_t_vector;
    typedef typename parser::string_token string_token;

    static void build_macros (const rules &rules_,
        macro_map &macro_map_, node_ptr_vector &node_ptr_vector_,
        charset_map &charset_map_, id_type &eol_id_)
    {
        const typename rules::string_pair_deque &macrodeque_ =
            rules_.macrodeque ();

        for (typename rules::string_pair_deque::const_iterator iter_ =
            macrodeque_.begin (), end_ = macrodeque_.end ();
            iter_ != end_; ++iter_)
        {
            const typename rules::string &name_ = iter_->first;
            const typename rules::string &regex_ = iter_->second;
            parser parser_ (rules_.locale (), node_ptr_vector_, macro_map_,
                charset_map_, rules_.eoi ());
            node *node_ = parser_.parse (regex_.c_str (),
                regex_.c_str () + regex_.size (), 0, 0, 0, false, false,
                rules_.flags (), eol_id_, false, true);
            macro_iter_pair map_iter_ = macro_map_.insert (macro_pair (name_,
                static_cast<const node *>(0)));

            map_iter_.first->second = node_;
        }
    }

    static void build_dfa (const charset_map &charset_map_, const node *root_,
        internals &internals_, sm &sm_, const id_type dfa_index_,
        id_type &eol_id_)
    {
        // partitioned charset list
        charset_list charset_list_;
        // vector mapping token indexes to partitioned token index sets
        index_set_vector set_mapping_;
        typename internals::id_type_vector &dfa_ =
            *internals_._dfa[dfa_index_];
        std::size_t dfa_alphabet_ = 0;
        const node_vector *followpos_ = &root_->firstpos ();
        node_set_vector seen_sets_;
        node_vector_vector seen_vectors_;
        size_t_vector hash_vector_;
        id_type_map newline_clash_map_;

        set_mapping_.resize (charset_map_.size ());
        partition_charsets (charset_map_, charset_list_, is_dfa ());
        build_set_mapping (charset_list_, internals_, dfa_index_,
            set_mapping_);

        if (eol_id_ != sm_traits::npos ())
        {
            eol_id_ = *set_mapping_[eol_id_].begin ();
        }

        dfa_alphabet_ = charset_list_->size () + transitions_index +
            (eol_id_ == sm_traits::npos () ? 0 : 1);

        if (dfa_alphabet_ > sm_traits::npos ())
        {
            // Overflow
            throw runtime_error ("The data type you have chosen cannot hold "
                "the dfa alphabet.");
        }

        internals_._dfa_alphabet[dfa_index_] = dfa_alphabet_;
        // 'jam' state
        dfa_.resize (dfa_alphabet_, 0);
        closure (followpos_, seen_sets_, seen_vectors_, hash_vector_,
            dfa_alphabet_, dfa_);

        for (id_type index_ = 0; index_ < static_cast<id_type>
            (seen_vectors_->size ()); ++index_)
        {
            equivset_list equiv_list_;
            id_type eol_next_ = 0;
            id_type newline_next_ = 0;

            build_equiv_list (seen_vectors_[index_], set_mapping_,
                equiv_list_, is_dfa ());

            for (typename equivset_list::list::const_iterator iter_ =
                equiv_list_->begin (), end_ = equiv_list_->end ();
                iter_ != end_; ++iter_)
            {
                equivset *equivset_ = *iter_;
                const id_type transition_ = closure
                    (&equivset_->_followpos, seen_sets_, seen_vectors_,
                    hash_vector_, dfa_alphabet_, dfa_);

                if (transition_ != sm_traits::npos ())
                {
                    id_type *ptr_ = &dfa_.front () + ((index_ + 1) *
                        dfa_alphabet_);

                    // Prune abstemious transitions from end states.
                    if (*ptr_ && !equivset_->_greedy) continue;

                    for (typename equivset::index_vector::const_iterator
                        equiv_iter_ = equivset_->_index_vector.begin (),
                        equiv_end_ = equivset_->_index_vector.end ();
                        equiv_iter_ != equiv_end_; ++equiv_iter_)
                    {
                        const id_type i_ = *equiv_iter_;

                        if (i_ == parser::bol_token ())
                        {
                            dfa_.front () = transition_;
                        }
                        else if (i_ == parser:: eol_token ())
                        {
                            ptr_[eol_index] = transition_;
                            eol_next_ = transition_;
                        }
                        else
                        {
                            ptr_[i_ + transitions_index] = transition_;

                            if (i_ == eol_id_)
                            {
                                newline_next_ = transition_;
                            }
                        }
                    }

                    if (eol_next_ && newline_next_)
                    {
                        newline_clash_map_.insert (id_type_pair (eol_next_,
                            newline_next_));
                        ptr_[eol_id_ + transitions_index] = 0;
                    }
                }
            }
        }

        fix_clashes (newline_clash_map_, eol_id_, dfa_, dfa_alphabet_,
            compressed ());
        append_dfa (charset_list_, internals_, sm_, dfa_index_, lookup ());
    }

    // Uncompressed
    static void fix_clashes (const id_type_map &newline_clash_map_,
        const id_type eol_id_, typename internals::id_type_vector &dfa_,
        const std::size_t dfa_alphabet_, const false_ &)
    {
        typename id_type_map::const_iterator clash_iter_ =
            newline_clash_map_.begin ();
        typename id_type_map::const_iterator clash_end_ =
            newline_clash_map_.end ();

        for (; clash_iter_ != clash_end_; ++clash_iter_)
        {
            id_type *ptr_ = &dfa_.front () + ((clash_iter_->first) *
                dfa_alphabet_);

            ptr_[eol_id_ + transitions_index] = clash_iter_->second;
        }
    }

    // Compressed
    static void fix_clashes (const id_type_map &/*newline_clash_map_*/,
        const id_type /*eol_id_*/, typename internals::id_type_vector &/*dfa_*/,
        const std::size_t /*dfa_alphabet_*/, const true_ &)
    {
        // Not currently supported
    }

    // char_state_machine version
    static void append_dfa (const charset_list &charset_list_,
        const internals &internals_, sm &sm_, const id_type dfa_index_,
        const false_ &)
    {
        typename charset_list::list::const_iterator list_iter_ =
            charset_list_->begin ();
        std::size_t size_ = charset_list_->size ();
        typename sm::string_token_vector token_vector_;

        token_vector_.reserve (size_);

        for (std::size_t i_ = 0; i_ < size_; ++i_, ++list_iter_)
        {
            const charset *charset_ = *list_iter_;

            token_vector_.push_back (charset_->_token);
        }

        sm_.append (token_vector_, internals_, dfa_index_);
    }

    // state_machine version
    static void append_dfa (const charset_list &,
        const internals &, sm &, const id_type, const true_ &)
    {
        // Nothing to do - will use create() instead
    }

    // char_state_machine version
    static void create (internals &, sm &, const id_type_vector &,
        const false_ &)
    {
        // Nothing to do - used append_dfa() instead
    }

    // state_machine version
    static void create (internals &internals_, sm &sm_,
        const id_type_vector &features_, const true_ &)
    {
        for (std::size_t i_ = 0, size_ = internals_._dfa->size ();
            i_ < size_; ++i_)
        {
            internals_._features |= features_[i_];
        }

        sm_.data ().swap (internals_);
    }

    // NFA version
    static void partition_charsets (const charset_map &map_,
        charset_list &lhs_, const false_ &)
    {
        fill_rhs_list (map_, lhs_);
    }

    // DFA version
    static void partition_charsets (const charset_map &map_,
        charset_list &lhs_, const true_ &)
    {
        charset_list rhs_;

        fill_rhs_list (map_, rhs_);

        if (!rhs_->empty ())
        {
            typename charset_list::list::iterator iter_;
            typename charset_list::list::iterator end_;
            charset_ptr overlap_ (new charset);

            lhs_->push_back (static_cast<charset *>(0));
            lhs_->back () = rhs_->front ();
            rhs_->pop_front ();

            while (!rhs_->empty ())
            {
                charset_ptr r_ (rhs_->front ());

                rhs_->pop_front ();
                iter_ = lhs_->begin ();
                end_ = lhs_->end ();

                while (!r_->empty () && iter_ != end_)
                {
                    typename charset_list::list::iterator l_iter_ = iter_;

                    (*l_iter_)->intersect (*r_.get (), *overlap_.get ());

                    if (overlap_->empty ())
                    {
                        ++iter_;
                    }
                    else if ((*l_iter_)->empty ())
                    {
                        delete *l_iter_;
                        *l_iter_ = overlap_.release ();
                        overlap_.reset (new charset);
                        ++iter_;
                    }
                    else if (r_->empty ())
                    {
                        delete r_.release ();
                        r_ = overlap_;
                        overlap_.reset (new charset);
                        break;
                    }
                    else
                    {
                        iter_ = lhs_->insert (++iter_,
                            static_cast<charset *>(0));
                        *iter_ = overlap_.release ();
                        overlap_.reset (new charset);
                        ++iter_;
                        end_ = lhs_->end ();
                    }
                }

                if (!r_->empty ())
                {
                    lhs_->push_back (static_cast<charset *>(0));
                    lhs_->back () = r_.release ();
                }
            }
        }
    }

    static void fill_rhs_list (const charset_map &map_,
        charset_list &list_)
    {
        typename charset_map::const_iterator iter_ = map_.begin ();
        typename charset_map::const_iterator end_ = map_.end ();

        for (; iter_ != end_; ++iter_)
        {
            list_->push_back (static_cast<charset *>(0));
            list_->back () = new charset (iter_->first, iter_->second);
        }
    }

    static void build_set_mapping (const charset_list &charset_list_,
        internals &internals_, const id_type dfa_index_,
        index_set_vector &set_mapping_)
    {
        typename charset_list::list::const_iterator iter_ =
            charset_list_->begin ();
        typename charset_list::list::const_iterator end_ =
            charset_list_->end ();
        typename index_set::const_iterator set_iter_;
        typename index_set::const_iterator set_end_;

        for (id_type index_ = 0; iter_ != end_; ++iter_, ++index_)
        {
            const charset *cs_ = *iter_;

            set_iter_ = cs_->_index_set.begin ();
            set_end_ = cs_->_index_set.end ();
            fill_lookup (cs_->_token, internals_._lookup[dfa_index_],
                index_, lookup ());

            for (; set_iter_ != set_end_; ++set_iter_)
            {
                set_mapping_[*set_iter_].insert (index_);
            }
        }
    }

    // char_state_machine version
    static void fill_lookup (const string_token &, id_type_vector *,
        const id_type, const false_ &)
    {
        // Do nothing (lookup not used)
    }

    // state_machine version
    static void fill_lookup (const string_token &charset_,
        id_type_vector *lookup_, const id_type index_, const true_ &)
    {
        typename string_token::range_vector::const_iterator iter_ =
            charset_._ranges.begin ();
        typename string_token::range_vector::const_iterator end_ =
            charset_._ranges.end ();
        id_type *ptr_ = &lookup_->front ();

        for (; iter_ != end_; ++iter_)
        {
            for (typename char_traits::index_type char_ = iter_->first;
                char_ < iter_->second; ++char_)
            {
                ptr_[static_cast<typename char_traits::index_type>
                    (char_)] = index_ + transitions_index;
            }

            ptr_[static_cast<typename char_traits::index_type>
                (iter_->second)] = index_ + transitions_index;
        }
    }

    static id_type closure (const node_vector *followpos_,
        node_set_vector &seen_sets_, node_vector_vector &seen_vectors_,
        size_t_vector &hash_vector_, const id_type size_, id_type_vector &dfa_)
    {
        bool end_state_ = false;
        id_type id_ = 0;
        id_type user_id_ = sm_traits::npos ();
        id_type next_dfa_ = 0;
        id_type push_dfa_ = sm_traits::npos ();
        bool pop_dfa_ = false;
        std::size_t hash_ = 0;

        if (followpos_->empty ()) return sm_traits::npos ();

        id_type index_ = 0;
        std::auto_ptr<node_set> set_ptr_ (new node_set);
        std::auto_ptr<node_vector> vector_ptr_ (new node_vector);

        for (typename node_vector::const_iterator iter_ =
            followpos_->begin (), end_ = followpos_->end ();
            iter_ != end_; ++iter_)
        {
            closure_ex (*iter_, end_state_, id_, user_id_, next_dfa_,
                push_dfa_, pop_dfa_, set_ptr_.get (),
                vector_ptr_.get (), hash_);
        }

        bool found_ = false;
        typename size_t_vector::const_iterator hash_iter_ =
            hash_vector_.begin ();
        typename size_t_vector::const_iterator hash_end_ =
            hash_vector_.end ();
        typename node_set_vector::vector::const_iterator set_iter_ =
            seen_sets_->begin ();

        for (; hash_iter_ != hash_end_; ++hash_iter_, ++set_iter_)
        {
            found_ = *hash_iter_ == hash_ && *(*set_iter_) == *set_ptr_;
            ++index_;

            if (found_) break;
        }

        if (!found_)
        {
            seen_sets_->push_back (static_cast<node_set *>(0));
            seen_sets_->back () = set_ptr_.release ();
            seen_vectors_->push_back (static_cast<node_vector *>(0));
            seen_vectors_->back () = vector_ptr_.release ();
            hash_vector_.push_back (hash_);
            // State 0 is the jam state...
            index_ = static_cast<id_type>(seen_sets_->size ());

            const std::size_t old_size_ = dfa_.size ();

            dfa_.resize (old_size_ + size_, 0);

            if (end_state_)
            {
                dfa_[old_size_] |= end_state_bit;

                if (pop_dfa_)
                {
                    dfa_[old_size_] |= pop_dfa_bit;
                }

                dfa_[old_size_ + id_index] = id_;
                dfa_[old_size_ + user_id_index] = user_id_;
                dfa_[old_size_ + push_dfa_index] = push_dfa_;
                dfa_[old_size_ + next_dfa_index] = next_dfa_;
            }
        }

        return index_;
    }

    static void closure_ex (node *node_, bool &end_state_,
        id_type &id_, id_type &user_id_, id_type &next_dfa_,
        id_type &push_dfa_, bool &pop_dfa_, node_set *set_ptr_,
        node_vector *vector_ptr_, std::size_t &hash_)
    {
        const bool temp_end_state_ = node_->end_state ();

        if (temp_end_state_)
        {
            if (!end_state_)
            {
                end_state_ = true;
                id_ = node_->id ();
                user_id_ = node_->user_id ();
                next_dfa_ = node_->next_dfa ();
                push_dfa_ = node_->push_dfa ();
                pop_dfa_ = node_->pop_dfa ();
            }
        }

        if (set_ptr_->insert (node_).second)
        {
            vector_ptr_->push_back (node_);
            hash_ += reinterpret_cast<std::size_t> (node_);
        }
    }

    // NFA version
    static void build_equiv_list (const node_vector *vector_,
        const index_set_vector &set_mapping_, equivset_list &lhs_,
        const false_ &)
    {
        fill_rhs_list (vector_, set_mapping_, lhs_);
    }

    // DFA version
    static void build_equiv_list (const node_vector *vector_,
        const index_set_vector &set_mapping_, equivset_list &lhs_,
        const true_ &)
    {
        equivset_list rhs_;

        fill_rhs_list (vector_, set_mapping_, rhs_);

        if (!rhs_->empty ())
        {
            typename equivset_list::list::iterator iter_;
            typename equivset_list::list::iterator end_;
            equivset_ptr overlap_ (new equivset);

            lhs_->push_back (static_cast<equivset *>(0));
            lhs_->back () = rhs_->front ();
            rhs_->pop_front ();

            while (!rhs_->empty ())
            {
                equivset_ptr r_ (rhs_->front ());

                rhs_->pop_front ();
                iter_ = lhs_->begin ();
                end_ = lhs_->end ();

                while (!r_->empty () && iter_ != end_)
                {
                    typename equivset_list::list::iterator l_iter_ = iter_;

                    (*l_iter_)->intersect (*r_.get (), *overlap_.get ());

                    if (overlap_->empty ())
                    {
                        ++iter_;
                    }
                    else if ((*l_iter_)->empty ())
                    {
                        delete *l_iter_;
                        *l_iter_ = overlap_.release ();
                        overlap_.reset (new equivset);
                        ++iter_;
                    }
                    else if (r_->empty ())
                    {
                        delete r_.release ();
                        r_ = overlap_;
                        overlap_.reset (new equivset);
                        break;
                    }
                    else
                    {
                        iter_ = lhs_->insert (++iter_,
                            static_cast<equivset *>(0));
                        *iter_ = overlap_.release ();
                        overlap_.reset (new equivset);
                        ++iter_;
                        end_ = lhs_->end ();
                    }
                }

                if (!r_->empty ())
                {
                    lhs_->push_back (static_cast<equivset *>(0));
                    lhs_->back () = r_.release ();
                }
            }
        }
    }

    static void fill_rhs_list (const node_vector *vector_,
        const index_set_vector &set_mapping_, equivset_list &list_)
    {
        typename node_vector::const_iterator iter_ =
            vector_->begin ();
        typename node_vector::const_iterator end_ =
            vector_->end ();

        for (; iter_ != end_; ++iter_)
        {
            const node *node_ = *iter_;

            if (!node_->end_state ())
            {
                const id_type token_ = node_->token ();

                if (token_ != node::null_token ())
                {
                    list_->push_back (static_cast<equivset *>(0));

                    if (token_ == parser::bol_token () ||
                        token_ == parser::eol_token ())
                    {
                        std::set<id_type> index_set_;

                        index_set_.insert (token_);
                        list_->back () = new equivset (index_set_,
                            token_, node_->greedy (), node_->followpos ());
                    }
                    else
                    {
                        list_->back () = new equivset (set_mapping_[token_],
                            token_, node_->greedy (), node_->followpos ());
                    }
                }
            }
        }
    }
};

typedef basic_generator<rules, state_machine> generator;
typedef basic_generator<wrules, wstate_machine> wgenerator;
typedef basic_generator<rules, char_state_machine> char_generator;
typedef basic_generator<wrules, wchar_state_machine> wchar_generator;
}

#endif
