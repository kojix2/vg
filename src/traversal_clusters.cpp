#include "traversal_clusters.hpp"

//#define debug

namespace vg {


// specialized version of jaccard_coefficient() that weights jaccard by node lenths. 
double weighted_jaccard_coefficient(const PathHandleGraph* graph,
                                    const multiset<handle_t>& target,
                                    const multiset<handle_t>& query) {
    vector<handle_t> intersection_handles;
    std::set_intersection(target.begin(), target.end(),
                          query.begin(), query.end(),
                          std::back_inserter(intersection_handles));
    vector<handle_t> union_handles;
    std::set_union(target.begin(), target.end(),
                   query.begin(), query.end(),
                   std::back_inserter(union_handles));

    int64_t isec_size = 0;
    for (const handle_t& handle : intersection_handles) {
        isec_size += graph->get_length(handle);
    }
    int64_t union_size = 0;
    for (const handle_t& handle : union_handles) {
        union_size += graph->get_length(handle);
    }    
    return (double)isec_size / (double)union_size;
}


vector<int> get_traversal_order(const PathHandleGraph* graph,
                                const vector<Traversal>& traversals,
                                const vector<string>& trav_path_names,
                                const vector<int>& ref_travs,
                                const vector<bool>& use_traversal) {
    set<int> ref_set(ref_travs.begin(), ref_travs.end());
    
    function<bool(int, int)> trav_less = [&](int i, int j) {
        // give reference set preference priority
        bool iref = ref_set.count(i);
        bool jref = ref_set.count(j);
        if (iref != jref) {
            return iref;
        }
        // fall back to name comparison 
        if (!trav_path_names[i].empty() && (trav_path_names[j].empty() || trav_path_names[i] < trav_path_names[j])) {
            return true;
        }
        return false;
    };

    vector<int> sorted_travs;
    sorted_travs.reserve(traversals.size());
    for (int64_t i = 0; i < traversals.size(); ++i) {
        if (use_traversal[i]) {
            sorted_travs.push_back(i);
        }
    }
#ifdef debug
    cerr << "names:";
    for (auto x : trav_path_names) cerr << " " << x;
    cerr << endl << "ref set:";
    for (auto x : ref_set) cerr << " " << x;
    cerr << endl << "before sort:";
    for (auto x : sorted_travs) cerr << " " << x;
#endif
    std::sort(sorted_travs.begin(), sorted_travs.end(), trav_less);
#ifdef debug
    cerr << endl << "after sort:";
    for (auto x : sorted_travs) cerr << " " << x;
    cerr << endl;
#endif
    assert(ref_travs.empty() || sorted_travs.empty() || ref_set.count(sorted_travs.front()));
    return sorted_travs;
}

vector<vector<int>> cluster_traversals(const PathHandleGraph* graph,
                                       const vector<Traversal>& traversals,
                                       const vector<int>& traversal_order,
                                       double min_jaccard,
                                       vector<pair<double, int64_t>>& out_info) {
    
    assert(traversal_order.size() <= traversals.size());
    
    // the values are indexes in the input traversals vector
    // the "reference" traversal of each cluster (to which distance is computed)
    // is always its first element
    vector<vector<int>> clusters;

    out_info.resize(traversals.size(), make_pair(-1., 0));
    
    // need the clusters as sorted lists. we'll forget the endpoints while we're at
    // it since they're always shared.  note we work with multisets since we want to
    // count differences between, say, cycle copy numbers. 
    vector<multiset<handle_t>> sorted_traversals;
    for (const Traversal& trav : traversals) {
        assert(trav.size() >=2 );
        // prune snarl nodes as they're always the same
        // todo: does jaccard properly handle empty sets?
        int64_t first = trav.size() == 2 ? 0 : 1;
        int64_t last = trav.size() == 2 ? trav.size() : trav.size() - 1;
        multiset<handle_t> sorted_trav;
        for (int64_t i = first; i < last; ++i) {
            sorted_trav.insert(trav[i]);
        }
        sorted_traversals.push_back(sorted_trav);
    } 

    for (const int& i : traversal_order) {
        const auto& trav = sorted_traversals[i];
        double max_jaccard = 0;
        int64_t max_cluster_idx = -1;
        for (int64_t j = 0; j < clusters.size(); ++j) {
            const auto& cluster_trav = sorted_traversals[clusters[j][0]];
            double jac = weighted_jaccard_coefficient(graph, trav, cluster_trav);
            if (jac > max_jaccard) {
                max_jaccard = jac;
                max_cluster_idx = j;
                if (jac == 1) {
                    break;
                }                    
            }        
        }
        if (max_cluster_idx >= 0 && max_jaccard >= min_jaccard) {
            // we've found a suitably similar cluster, add it do that
            clusters[max_cluster_idx].push_back(i);
            out_info[i] = make_pair(max_jaccard, 0);
        } else {
            // there's no cluster close enough, need to start a new one
            clusters.push_back({i});
            out_info[i] = make_pair(1.0, 0);
        }        
    }

    // fill in the size deltas
    for (vector<int>& cluster : clusters) {
        // only non-zero for non-empty clusters
        if (cluster.size() > 1) {
            int64_t cluster_ref_length = -1;
            for (int64_t i = 1; i < cluster.size(); ++i) {
                if (out_info[cluster[i]].first < 1) {
                    // get the cluster reference length on-demand
                    if (cluster_ref_length == -1) {
                        cluster_ref_length = 0;
                        for (const handle_t& handle : traversals[cluster[0]]) {
                            cluster_ref_length += graph->get_length(handle);
                        }
                    }
                    // compute the length of the non-ref traversal
                    int64_t length = 0;
                    for (const handle_t& handle : traversals[cluster[i]]) {
                        length += graph->get_length(handle);
                    }
                    // store the delta
                   out_info[cluster[i]].second = length - cluster_ref_length;
                }
                
            }
        }
    }

    return clusters;
}


}
