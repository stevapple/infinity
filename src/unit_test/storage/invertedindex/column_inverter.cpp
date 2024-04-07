// Copyright(C) 2023 InfiniFlow, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "unit_test/base_test.h"

import stl;
import analyzer;
import analyzer_pool;
import memory_pool;
import pool_allocator;
import index_defines;
import posting_writer;
import posting_list_format;
import column_vector;
import data_type;
import value;
import column_inverter;
import segment_posting;
import posting_iterator;
import internal_types;
import logical_type;
import local_file_system;
import segment_index_entry;
import column_length_io;

using namespace infinity;

class ColumnInverterTest : public BaseTest {
protected:
    MemoryPool byte_slice_pool_{};
    RecyclePool buffer_pool_{};
    optionflag_t flag_{OPTION_FLAG_ALL};
    Map<String, SharedPtr<PostingWriter>> postings_;
    std::shared_mutex column_length_mutex_;
    Vector<u32> column_length_array_;

public:
    struct ExpectedPosting {
        String term;
        Vector<RowID> doc_ids;
        Vector<u32> tfs;
    };

public:
    void SetUp() override {}

    void TearDown() override {}

    SharedPtr<PostingWriter> GetOrAddPosting(const String &term) {
        auto it = postings_.find(term);
        if (it != postings_.end()) {
            return it->second;
        }
        SharedPtr<PostingWriter> posting =
            MakeShared<PostingWriter>(&byte_slice_pool_, &buffer_pool_, PostingFormatOption(flag_), column_length_mutex_, column_length_array_);
        postings_[term] = posting;
        return posting;
    }
};

TEST_F(ColumnInverterTest, Invert) {
    // https://en.wikipedia.org/wiki/Finite-state_transducer
    const char *paragraphs[] = {
        R"#(A finite-state transducer (FST) is a finite-state machine with two memory tapes, following the terminology for Turing machines: an input tape and an output tape. This contrasts with an ordinary finite-state automaton, which has a single tape. An FST is a type of finite-state automaton (FSA) that maps between two sets of symbols.[1] An FST is more general than an FSA. An FSA defines a formal language by defining a set of accepted strings, while an FST defines a relation between sets of strings.)#",
        R"#(An FST will read a set of strings on the input tape and generates a set of relations on the output tape. An FST can be thought of as a translator or relater between strings in a set.)#",
        R"#(In morphological parsing, an example would be inputting a string of letters into the FST, the FST would then output a string of morphemes.)#",
        R"#(An automaton can be said to recognize a string if we view the content of its tape as input. In other words, the automaton computes a function that maps strings into the set {0,1}. Alternatively, we can say that an automaton generates strings, which means viewing its tape as an output tape. On this view, the automaton generates a formal language, which is a set of strings. The two views of automata are equivalent: the function that the automaton computes is precisely the indicator function of the set of strings it generates. The class of languages generated by finite automata is known as the class of regular languages.)#",
        R"#(The two tapes of a transducer are typically viewed as an input tape and an output tape. On this view, a transducer is said to transduce (i.e., translate) the contents of its input tape to its output tape, by accepting a string on its input tape and generating another string on its output tape. It may do so nondeterministically and it may produce more than one output for each input string. A transducer may also produce no output for a given input string, in which case it is said to reject the input. In general, a transducer computes a relation between two formal languages.)#",
    };
    const SizeT num_paragraph = sizeof(paragraphs) / sizeof(char *);
    SharedPtr<ColumnVector> column = ColumnVector::Make(MakeShared<DataType>(LogicalType::kVarchar));
    column->Initialize();
    for (SizeT i = 0; i < num_paragraph; ++i) {
        Value v = Value::MakeVarchar(String(paragraphs[i]));
        column->AppendValue(v);
    }
    Vector<ExpectedPosting> expected_postings = {{"fst", {0, 1, 2}, {4, 2, 2}}, {"automaton", {0, 3}, {2, 5}}, {"transducer", {0, 4}, {1, 4}}};

    auto fake_segment_index_entry = SegmentIndexEntry::CreateFakeEntry();
    String folder = "/tmp/infinity/test_column_inverter/chunk1";
    auto fs = MakeUnique<LocalFileSystem>();
    fs->CreateDirectory(folder);
    String column_length_file_path = folder + LENGTH_SUFFIX;
    auto column_length_file_handler =
        MakeShared<FullTextColumnLengthFileHandler>(std::move(fs), column_length_file_path, fake_segment_index_entry.get());
    auto update_length_job_1 =
        MakeShared<FullTextColumnLengthUpdateJob>(column_length_file_handler, 3, 0, column_length_mutex_, column_length_array_);
    auto update_length_job_2 =
        MakeShared<FullTextColumnLengthUpdateJob>(std::move(column_length_file_handler), 2, 3, column_length_mutex_, column_length_array_);
    PostingWriterProvider provider = [this](const String &term) -> SharedPtr<PostingWriter> { return GetOrAddPosting(term); };
    ColumnInverter inverter1("standard", provider);
    ColumnInverter inverter2("standard", provider);
    inverter1.InvertColumn(column, 0, 3, 0);
    inverter2.InvertColumn(column, 3, 2, 3);
    inverter1.GetTermListLength(update_length_job_1->GetColumnLengthArray());
    inverter2.GetTermListLength(update_length_job_2->GetColumnLengthArray());
    update_length_job_1->DumpToFile();
    update_length_job_2->DumpToFile();
    update_length_job_1.reset();
    update_length_job_2.reset();

    inverter1.Merge(inverter2);

    inverter1.Sort();

    inverter1.GeneratePosting();

    for (SizeT i = 0; i < expected_postings.size(); ++i) {
        const ExpectedPosting &expected = expected_postings[i];
        const String &term = expected.term;
        auto it = postings_.find(term);
        ASSERT_TRUE(it != postings_.end());
        SharedPtr<PostingWriter> posting = it->second;
        ASSERT_TRUE(posting != nullptr);
        ASSERT_EQ(posting->GetDF(), expected.doc_ids.size());

        SharedPtr<Vector<SegmentPosting>> seg_postings = MakeShared<Vector<SegmentPosting>>(1);
        seg_postings->at(0).Init(u64(0), posting);

        PostingIterator post_iter(flag_, &byte_slice_pool_);
        post_iter.Init(seg_postings, 0);

        RowID doc_id = INVALID_ROWID;
        for (SizeT j = 0; j < expected.doc_ids.size(); ++j) {
            doc_id = post_iter.SeekDoc(expected.doc_ids[j]);
            ASSERT_EQ(doc_id, expected.doc_ids[j]);
            u32 tf = post_iter.GetCurrentTF();
            ASSERT_EQ(tf, expected.tfs[j]);
        }
        if (doc_id != INVALID_ROWID) {
            doc_id = post_iter.SeekDoc(doc_id + 1);
            ASSERT_EQ(doc_id, INVALID_ROWID);
        }

        pos_t pos = 0;
        u32 ii = 0;
        pos_t ret_occ = INVALID_POSITION;
        do {
            post_iter.SeekPosition(pos + ii, ret_occ);
            ++ii;
        } while (ret_occ != INVALID_POSITION);
    }
}
