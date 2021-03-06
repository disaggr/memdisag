// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "arrow/dataset/scanner.h"

#include <memory>

#include <gmock/gmock.h>

#include "arrow/compute/api.h"
#include "arrow/compute/api_scalar.h"
#include "arrow/compute/api_vector.h"
#include "arrow/compute/cast.h"
#include "arrow/dataset/scanner_internal.h"
#include "arrow/dataset/test_util.h"
#include "arrow/record_batch.h"
#include "arrow/table.h"
#include "arrow/testing/future_util.h"
#include "arrow/testing/generator.h"
#include "arrow/testing/gtest_util.h"
#include "arrow/testing/util.h"
#include "arrow/util/range.h"

using testing::ElementsAre;
using testing::IsEmpty;

namespace arrow {
namespace dataset {

struct TestScannerParams {
  bool use_async;
  bool use_threads;
  int num_child_datasets;
  int num_batches;
  int items_per_batch;

  std::string ToString() const {
    // GTest requires this to be alphanumeric
    std::stringstream ss;
    ss << (use_async ? "Async" : "Sync") << (use_threads ? "Threaded" : "Serial")
       << num_child_datasets << "d" << num_batches << "b" << items_per_batch << "r";
    return ss.str();
  }

  static std::string ToTestNameString(
      const ::testing::TestParamInfo<TestScannerParams>& info) {
    return std::to_string(info.index) + info.param.ToString();
  }

  static std::vector<TestScannerParams> Values() {
    std::vector<TestScannerParams> values;
    for (int sync = 0; sync < 2; sync++) {
      for (int use_threads = 0; use_threads < 2; use_threads++) {
        values.push_back(
            {static_cast<bool>(sync), static_cast<bool>(use_threads), 1, 1, 1024});
        values.push_back(
            {static_cast<bool>(sync), static_cast<bool>(use_threads), 2, 16, 1024});
      }
    }
    return values;
  }
};

std::ostream& operator<<(std::ostream& out, const TestScannerParams& params) {
  out << (params.use_async ? "async-" : "sync-")
      << (params.use_threads ? "threaded-" : "serial-") << params.num_child_datasets
      << "d-" << params.num_batches << "b-" << params.items_per_batch << "i";
  return out;
}

class TestScanner : public DatasetFixtureMixinWithParam<TestScannerParams> {
 protected:
  std::shared_ptr<Scanner> MakeScanner(std::shared_ptr<Dataset> dataset) {
    ScannerBuilder builder(std::move(dataset), options_);
    ARROW_EXPECT_OK(builder.UseThreads(GetParam().use_threads));
    ARROW_EXPECT_OK(builder.UseAsync(GetParam().use_async));
    EXPECT_OK_AND_ASSIGN(auto scanner, builder.Finish());
    return scanner;
  }

  std::shared_ptr<Scanner> MakeScanner(std::shared_ptr<RecordBatch> batch) {
    std::vector<std::shared_ptr<RecordBatch>> batches{
        static_cast<size_t>(GetParam().num_batches), batch};

    DatasetVector children{static_cast<size_t>(GetParam().num_child_datasets),
                           std::make_shared<InMemoryDataset>(batch->schema(), batches)};

    EXPECT_OK_AND_ASSIGN(auto dataset, UnionDataset::Make(batch->schema(), children));
    return MakeScanner(std::move(dataset));
  }

  void AssertScannerEqualsRepetitionsOf(
      std::shared_ptr<Scanner> scanner, std::shared_ptr<RecordBatch> batch,
      const int64_t total_batches = GetParam().num_child_datasets *
                                    GetParam().num_batches) {
    auto expected = ConstantArrayGenerator::Repeat(total_batches, batch);

    // Verifies that the unified BatchReader is equivalent to flattening all the
    // structures of the scanner, i.e. Scanner[Dataset[ScanTask[RecordBatch]]]
    AssertScannerEquals(expected.get(), scanner.get());
  }

  void AssertScanBatchesEqualRepetitionsOf(
      std::shared_ptr<Scanner> scanner, std::shared_ptr<RecordBatch> batch,
      const int64_t total_batches = GetParam().num_child_datasets *
                                    GetParam().num_batches) {
    auto expected = ConstantArrayGenerator::Repeat(total_batches, batch);

    AssertScanBatchesEquals(expected.get(), scanner.get());
  }

  void AssertScanBatchesUnorderedEqualRepetitionsOf(
      std::shared_ptr<Scanner> scanner, std::shared_ptr<RecordBatch> batch,
      const int64_t total_batches = GetParam().num_child_datasets *
                                    GetParam().num_batches) {
    auto expected = ConstantArrayGenerator::Repeat(total_batches, batch);

    AssertScanBatchesUnorderedEquals(expected.get(), scanner.get(), 1);
  }
};

TEST_P(TestScanner, Scan) {
  SetSchema({field("i32", int32()), field("f64", float64())});
  auto batch = ConstantArrayGenerator::Zeroes(GetParam().items_per_batch, schema_);
  AssertScanBatchesUnorderedEqualRepetitionsOf(MakeScanner(batch), batch);
}

TEST_P(TestScanner, ScanBatches) {
  SetSchema({field("i32", int32()), field("f64", float64())});
  auto batch = ConstantArrayGenerator::Zeroes(GetParam().items_per_batch, schema_);
  AssertScanBatchesEqualRepetitionsOf(MakeScanner(batch), batch);
}

TEST_P(TestScanner, ScanBatchesUnordered) {
  SetSchema({field("i32", int32()), field("f64", float64())});
  auto batch = ConstantArrayGenerator::Zeroes(GetParam().items_per_batch, schema_);
  AssertScanBatchesUnorderedEqualRepetitionsOf(MakeScanner(batch), batch);
}

TEST_P(TestScanner, ScanWithCappedBatchSize) {
  SetSchema({field("i32", int32()), field("f64", float64())});
  auto batch = ConstantArrayGenerator::Zeroes(GetParam().items_per_batch, schema_);
  options_->batch_size = GetParam().items_per_batch / 2;
  auto expected = batch->Slice(GetParam().items_per_batch / 2);
  AssertScanBatchesEqualRepetitionsOf(
      MakeScanner(batch), expected,
      GetParam().num_child_datasets * GetParam().num_batches * 2);
}

TEST_P(TestScanner, FilteredScan) {
  SetSchema({field("f64", float64())});

  double value = 0.5;
  ASSERT_OK_AND_ASSIGN(auto f64,
                       ArrayFromBuilderVisitor(float64(), GetParam().items_per_batch,
                                               GetParam().items_per_batch / 2,
                                               [&](DoubleBuilder* builder) {
                                                 builder->UnsafeAppend(value);
                                                 builder->UnsafeAppend(-value);
                                                 value += 1.0;
                                               }));

  SetFilter(greater(field_ref("f64"), literal(0.0)));

  auto batch = RecordBatch::Make(schema_, f64->length(), {f64});

  value = 0.5;
  ASSERT_OK_AND_ASSIGN(auto f64_filtered,
                       ArrayFromBuilderVisitor(float64(), GetParam().items_per_batch / 2,
                                               [&](DoubleBuilder* builder) {
                                                 builder->UnsafeAppend(value);
                                                 value += 1.0;
                                               }));

  auto filtered_batch =
      RecordBatch::Make(schema_, f64_filtered->length(), {f64_filtered});

  AssertScanBatchesEqualRepetitionsOf(MakeScanner(batch), filtered_batch);
}

TEST_P(TestScanner, ProjectedScan) {
  SetSchema({field("i32", int32()), field("f64", float64())});
  SetProjectedColumns({"i32"});
  auto batch_in = ConstantArrayGenerator::Zeroes(GetParam().items_per_batch, schema_);
  auto batch_out = ConstantArrayGenerator::Zeroes(GetParam().items_per_batch,
                                                  schema({field("i32", int32())}));
  AssertScanBatchesUnorderedEqualRepetitionsOf(MakeScanner(batch_in), batch_out);
}

TEST_P(TestScanner, MaterializeMissingColumn) {
  SetSchema({field("i32", int32()), field("f64", float64())});
  auto batch_missing_f64 = ConstantArrayGenerator::Zeroes(
      GetParam().items_per_batch, schema({field("i32", int32())}));

  auto fragment_missing_f64 = std::make_shared<InMemoryFragment>(
      RecordBatchVector{
          static_cast<size_t>(GetParam().num_child_datasets * GetParam().num_batches),
          batch_missing_f64},
      equal(field_ref("f64"), literal(2.5)));

  ASSERT_OK_AND_ASSIGN(auto f64,
                       ArrayFromBuilderVisitor(
                           float64(), GetParam().items_per_batch,
                           [&](DoubleBuilder* builder) { builder->UnsafeAppend(2.5); }));
  auto batch_with_f64 =
      RecordBatch::Make(schema_, f64->length(), {batch_missing_f64->column(0), f64});

  FragmentVector fragments{fragment_missing_f64};
  auto dataset = std::make_shared<FragmentDataset>(schema_, fragments);
  auto scanner = MakeScanner(std::move(dataset));
  AssertScanBatchesEqualRepetitionsOf(scanner, batch_with_f64);
}

TEST_P(TestScanner, ToTable) {
  SetSchema({field("i32", int32()), field("f64", float64())});
  auto batch = ConstantArrayGenerator::Zeroes(GetParam().items_per_batch, schema_);
  std::vector<std::shared_ptr<RecordBatch>> batches{
      static_cast<std::size_t>(GetParam().num_batches * GetParam().num_child_datasets),
      batch};

  ASSERT_OK_AND_ASSIGN(auto expected, Table::FromRecordBatches(batches));

  auto scanner = MakeScanner(batch);
  std::shared_ptr<Table> actual;

  // There is no guarantee on the ordering when using multiple threads, but
  // since the RecordBatch is always the same it will pass.
  ASSERT_OK_AND_ASSIGN(actual, scanner->ToTable());
  AssertTablesEqual(*expected, *actual);
}

TEST_P(TestScanner, ScanWithVisitor) {
  SetSchema({field("i32", int32()), field("f64", float64())});
  auto batch = ConstantArrayGenerator::Zeroes(GetParam().items_per_batch, schema_);
  auto scanner = MakeScanner(batch);
  ASSERT_OK(scanner->Scan([batch](TaggedRecordBatch scanned_batch) {
    AssertBatchesEqual(*batch, *scanned_batch.record_batch);
    return Status::OK();
  }));
}

TEST_P(TestScanner, TakeIndices) {
  auto batch_size = GetParam().items_per_batch;
  auto num_batches = GetParam().num_batches;
  auto num_datasets = GetParam().num_child_datasets;
  SetSchema({field("i32", int32()), field("f64", float64())});
  ArrayVector arrays(2);
  ArrayFromVector<Int32Type>(internal::Iota<int32_t>(batch_size), &arrays[0]);
  ArrayFromVector<DoubleType>(internal::Iota<double>(static_cast<double>(batch_size)),
                              &arrays[1]);
  auto batch = RecordBatch::Make(schema_, batch_size, arrays);

  auto scanner = MakeScanner(batch);

  std::shared_ptr<Array> indices;
  {
    ArrayFromVector<Int64Type>(internal::Iota(batch_size), &indices);
    ASSERT_OK_AND_ASSIGN(auto taken, scanner->TakeRows(*indices));
    ASSERT_OK_AND_ASSIGN(auto expected, Table::FromRecordBatches({batch}));
    ASSERT_EQ(expected->num_rows(), batch_size);
    AssertTablesEqual(*expected, *taken);
  }
  {
    ArrayFromVector<Int64Type>({7, 5, 3, 1}, &indices);
    ASSERT_OK_AND_ASSIGN(auto taken, scanner->TakeRows(*indices));
    ASSERT_OK_AND_ASSIGN(auto table, scanner->ToTable());
    ASSERT_OK_AND_ASSIGN(auto expected, compute::Take(table, *indices));
    ASSERT_EQ(expected.table()->num_rows(), 4);
    AssertTablesEqual(*expected.table(), *taken);
  }
  if (num_batches > 1) {
    ArrayFromVector<Int64Type>({batch_size + 2, batch_size + 1}, &indices);
    ASSERT_OK_AND_ASSIGN(auto table, scanner->ToTable());
    ASSERT_OK_AND_ASSIGN(auto taken, scanner->TakeRows(*indices));
    ASSERT_OK_AND_ASSIGN(auto expected, compute::Take(table, *indices));
    ASSERT_EQ(expected.table()->num_rows(), 2);
    AssertTablesEqual(*expected.table(), *taken);
  }
  if (num_batches > 1) {
    ArrayFromVector<Int64Type>({1, 3, 5, 7, batch_size + 1, 2 * batch_size + 2},
                               &indices);
    ASSERT_OK_AND_ASSIGN(auto taken, scanner->TakeRows(*indices));
    ASSERT_OK_AND_ASSIGN(auto table, scanner->ToTable());
    ASSERT_OK_AND_ASSIGN(auto expected, compute::Take(table, *indices));
    ASSERT_EQ(expected.table()->num_rows(), 6);
    AssertTablesEqual(*expected.table(), *taken);
  }
  {
    auto base = num_datasets * num_batches * batch_size;
    ArrayFromVector<Int64Type>({base + 1}, &indices);
    EXPECT_RAISES_WITH_MESSAGE_THAT(
        IndexError,
        ::testing::HasSubstr("Some indices were out of bounds: " +
                             std::to_string(base + 1)),
        scanner->TakeRows(*indices));
  }
  {
    auto base = num_datasets * num_batches * batch_size;
    ArrayFromVector<Int64Type>(
        {1, 2, base + 1, base + 2, base + 3, base + 4, base + 5, base + 6}, &indices);
    EXPECT_RAISES_WITH_MESSAGE_THAT(
        IndexError,
        ::testing::HasSubstr(
            "Some indices were out of bounds: " + std::to_string(base + 1) + ", " +
            std::to_string(base + 2) + ", " + std::to_string(base + 3) + ", ..."),
        scanner->TakeRows(*indices));
  }
}

class FailingFragment : public InMemoryFragment {
 public:
  using InMemoryFragment::InMemoryFragment;
  Result<ScanTaskIterator> Scan(std::shared_ptr<ScanOptions> options) override {
    int index = 0;
    auto self = shared_from_this();
    return MakeFunctionIterator([=]() mutable -> Result<std::shared_ptr<ScanTask>> {
      if (index > 16) {
        return Status::Invalid("Oh no, we failed!");
      }
      RecordBatchVector batches = {record_batches_[index++ % record_batches_.size()]};
      return std::make_shared<InMemoryScanTask>(batches, options, self);
    });
  }

  Result<RecordBatchGenerator> ScanBatchesAsync(
      const std::shared_ptr<ScanOptions>& options) override {
    struct {
      Future<std::shared_ptr<RecordBatch>> operator()() {
        if (index > 16) {
          return Status::Invalid("Oh no, we failed!");
        }
        auto batch = batches[index++ % batches.size()];
        return Future<std::shared_ptr<RecordBatch>>::MakeFinished(batch);
      }
      RecordBatchVector batches;
      int index = 0;
    } Generator;
    Generator.batches = record_batches_;
    return Generator;
  }
};

class FailingExecuteScanTask : public InMemoryScanTask {
 public:
  using InMemoryScanTask::InMemoryScanTask;

  Result<RecordBatchIterator> Execute() override {
    return Status::Invalid("Oh no, we failed!");
  }
};

class FailingIterationScanTask : public InMemoryScanTask {
 public:
  using InMemoryScanTask::InMemoryScanTask;

  Result<RecordBatchIterator> Execute() override {
    int index = 0;
    auto batches = record_batches_;
    return MakeFunctionIterator(
        [index, batches]() mutable -> Result<std::shared_ptr<RecordBatch>> {
          if (index < 1) {
            return batches[index++];
          }
          return Status::Invalid("Oh no, we failed!");
        });
  }
};

template <typename T>
class FailingScanTaskFragment : public InMemoryFragment {
 public:
  using InMemoryFragment::InMemoryFragment;
  Result<ScanTaskIterator> Scan(std::shared_ptr<ScanOptions> options) override {
    auto self = shared_from_this();
    ScanTaskVector scan_tasks{std::make_shared<T>(record_batches_, options, self)};
    return MakeVectorIterator(std::move(scan_tasks));
  }

  // Unlike the sync case, there's only two places to fail - during
  // iteration (covered by FailingFragment) or at the initial scan
  // (covered here)
  Result<RecordBatchGenerator> ScanBatchesAsync(
      const std::shared_ptr<ScanOptions>& options) override {
    return Status::Invalid("Oh no, we failed!");
  }
};

template <typename It, typename GetBatch>
bool CheckIteratorRaises(const RecordBatch& batch, It batch_it, GetBatch get_batch) {
  while (true) {
    auto maybe_batch = batch_it.Next();
    if (maybe_batch.ok()) {
      EXPECT_OK_AND_ASSIGN(auto scanned_batch, maybe_batch);
      if (IsIterationEnd(scanned_batch)) break;
      AssertBatchesEqual(batch, *get_batch(scanned_batch));
    } else {
      EXPECT_RAISES_WITH_MESSAGE_THAT(Invalid, ::testing::HasSubstr("Oh no, we failed!"),
                                      maybe_batch);
      return true;
    }
  }
  return false;
}

TEST_P(TestScanner, ScanBatchesFailure) {
  SetSchema({field("i32", int32()), field("f64", float64())});
  auto batch = ConstantArrayGenerator::Zeroes(GetParam().items_per_batch, schema_);
  RecordBatchVector batches = {batch, batch, batch, batch};

  auto check_scanner = [](const RecordBatch& batch, Scanner* scanner) {
    auto maybe_batch_it = scanner->ScanBatchesUnordered();
    if (!maybe_batch_it.ok()) {
      // SyncScanner can fail here as it eagerly consumes the first value
      EXPECT_RAISES_WITH_MESSAGE_THAT(Invalid, ::testing::HasSubstr("Oh no, we failed!"),
                                      std::move(maybe_batch_it));
    } else {
      ASSERT_OK_AND_ASSIGN(auto batch_it, std::move(maybe_batch_it));
      EXPECT_TRUE(CheckIteratorRaises(
          batch, std::move(batch_it),
          [](const EnumeratedRecordBatch& batch) { return batch.record_batch.value; }))
          << "ScanBatchesUnordered() did not raise an error";
    }
    ASSERT_OK_AND_ASSIGN(auto tagged_batch_it, scanner->ScanBatches());
    EXPECT_TRUE(CheckIteratorRaises(
        batch, std::move(tagged_batch_it),
        [](const TaggedRecordBatch& batch) { return batch.record_batch; }))
        << "ScanBatches() did not raise an error";
  };

  // Case 1: failure when getting next scan task
  {
    FragmentVector fragments{std::make_shared<FailingFragment>(batches)};
    auto dataset = std::make_shared<FragmentDataset>(schema_, fragments);
    auto scanner = MakeScanner(std::move(dataset));
    check_scanner(*batch, scanner.get());
  }

  // Case 2: failure when calling ScanTask::Execute
  {
    FragmentVector fragments{
        std::make_shared<FailingScanTaskFragment<FailingExecuteScanTask>>(batches)};
    auto dataset = std::make_shared<FragmentDataset>(schema_, fragments);
    auto scanner = MakeScanner(std::move(dataset));
    check_scanner(*batch, scanner.get());
  }

  // Case 3: failure when calling RecordBatchIterator::Next
  {
    FragmentVector fragments{
        std::make_shared<FailingScanTaskFragment<FailingIterationScanTask>>(batches)};
    auto dataset = std::make_shared<FragmentDataset>(schema_, fragments);
    auto scanner = MakeScanner(std::move(dataset));
    check_scanner(*batch, scanner.get());
  }
}

TEST_P(TestScanner, Head) {
  auto batch_size = GetParam().items_per_batch;
  auto num_batches = GetParam().num_batches;
  auto num_datasets = GetParam().num_child_datasets;
  SetSchema({field("i32", int32()), field("f64", float64())});
  auto batch = ConstantArrayGenerator::Zeroes(batch_size, schema_);

  auto scanner = MakeScanner(batch);
  std::shared_ptr<Table> expected, actual;

  ASSERT_OK_AND_ASSIGN(expected, Table::FromRecordBatches(schema_, {}));
  ASSERT_OK_AND_ASSIGN(actual, scanner->Head(0));
  AssertTablesEqual(*expected, *actual);

  ASSERT_OK_AND_ASSIGN(expected, Table::FromRecordBatches(schema_, {batch}));
  ASSERT_OK_AND_ASSIGN(actual, scanner->Head(batch_size));
  AssertTablesEqual(*expected, *actual);

  ASSERT_OK_AND_ASSIGN(expected, Table::FromRecordBatches(schema_, {batch->Slice(0, 1)}));
  ASSERT_OK_AND_ASSIGN(actual, scanner->Head(1));
  AssertTablesEqual(*expected, *actual);

  if (num_batches > 1) {
    ASSERT_OK_AND_ASSIGN(expected,
                         Table::FromRecordBatches(schema_, {batch, batch->Slice(0, 1)}));
    ASSERT_OK_AND_ASSIGN(actual, scanner->Head(batch_size + 1));
    AssertTablesEqual(*expected, *actual);
  }

  ASSERT_OK_AND_ASSIGN(expected, scanner->ToTable());
  ASSERT_OK_AND_ASSIGN(actual, scanner->Head(batch_size * num_batches * num_datasets));
  AssertTablesEqual(*expected, *actual);

  ASSERT_OK_AND_ASSIGN(expected, scanner->ToTable());
  ASSERT_OK_AND_ASSIGN(actual,
                       scanner->Head(batch_size * num_batches * num_datasets + 100));
  AssertTablesEqual(*expected, *actual);
}

INSTANTIATE_TEST_SUITE_P(TestScannerThreading, TestScanner,
                         ::testing::ValuesIn(TestScannerParams::Values()));

/// These ControlledXyz classes allow for controlling the order in which things are
/// delivered so that we can test out of order resequencing.  The dataset allows
/// batches to be delivered on any fragment.  When delivering batches a num_rows
/// parameter is taken which can be used to differentiate batches.
class ControlledFragment : public Fragment {
 public:
  explicit ControlledFragment(std::shared_ptr<Schema> schema)
      : Fragment(literal(true), std::move(schema)) {}

  Result<ScanTaskIterator> Scan(std::shared_ptr<ScanOptions> options) override {
    return Status::NotImplemented(
        "Not needed for testing.  Sync can only return things in-order.");
  }
  Result<std::shared_ptr<Schema>> ReadPhysicalSchemaImpl() override {
    return physical_schema_;
  }
  std::string type_name() const override { return "scanner_test.cc::ControlledFragment"; }

  Result<RecordBatchGenerator> ScanBatchesAsync(
      const std::shared_ptr<ScanOptions>& options) override {
    return record_batch_generator_;
  };

  void Finish() { ARROW_UNUSED(record_batch_generator_.producer().Close()); }
  void DeliverBatch(uint32_t num_rows) {
    auto batch = ConstantArrayGenerator::Zeroes(num_rows, physical_schema_);
    record_batch_generator_.producer().Push(std::move(batch));
  }

 private:
  PushGenerator<std::shared_ptr<RecordBatch>> record_batch_generator_;
};

// TODO(ARROW-8163) Add testing for fragments arriving out of order
class ControlledDataset : public Dataset {
 public:
  explicit ControlledDataset(int num_fragments)
      : Dataset(arrow::schema({field("i32", int32())})), fragments_() {
    for (int i = 0; i < num_fragments; i++) {
      fragments_.push_back(std::make_shared<ControlledFragment>(schema_));
    }
  }

  std::string type_name() const override { return "scanner_test.cc::ControlledDataset"; }
  Result<std::shared_ptr<Dataset>> ReplaceSchema(
      std::shared_ptr<Schema> schema) const override {
    return Status::NotImplemented("Should not be called by unit test");
  }

  void DeliverBatch(int fragment_index, int num_rows) {
    fragments_[fragment_index]->DeliverBatch(num_rows);
  }

  void FinishFragment(int fragment_index) { fragments_[fragment_index]->Finish(); }

 protected:
  Result<FragmentIterator> GetFragmentsImpl(compute::Expression predicate) override {
    std::vector<std::shared_ptr<Fragment>> casted_fragments(fragments_.begin(),
                                                            fragments_.end());
    return MakeVectorIterator(std::move(casted_fragments));
  }

 private:
  std::vector<std::shared_ptr<ControlledFragment>> fragments_;
};

constexpr int kNumFragments = 2;

class TestReordering : public ::testing::Test {
 public:
  void SetUp() override { dataset_ = std::make_shared<ControlledDataset>(kNumFragments); }

  // Given a vector of fragment indices (one per batch) return a vector
  // (one per fragment) mapping fragment index to the last occurrence of that
  // index in order
  //
  // This allows us to know when to mark a fragment as finished
  std::vector<int> GetLastIndices(const std::vector<int>& order) {
    std::vector<int> last_indices(kNumFragments);
    for (std::size_t i = 0; i < kNumFragments; i++) {
      auto last_p = std::find(order.rbegin(), order.rend(), static_cast<int>(i));
      EXPECT_NE(last_p, order.rend());
      last_indices[i] = static_cast<int>(std::distance(last_p, order.rend())) - 1;
    }
    return last_indices;
  }

  /// We buffer one item in order to enumerate it (technically this could be avoided if
  /// delivering in order but easier to have a single code path).  We also can't deliver
  /// items that don't come next.  These two facts make for some pretty complex logic
  /// to determine when items are ready to be collected.
  std::vector<TaggedRecordBatch> DeliverAndCollect(std::vector<int> order,
                                                   TaggedRecordBatchGenerator gen) {
    std::vector<TaggedRecordBatch> collected;
    auto last_indices = GetLastIndices(order);
    int num_fragments = static_cast<int>(last_indices.size());
    std::vector<int> batches_seen_for_fragment(num_fragments);
    auto current_fragment_index = 0;
    auto seen_fragment = false;
    for (std::size_t i = 0; i < order.size(); i++) {
      auto fragment_index = order[i];
      dataset_->DeliverBatch(fragment_index, static_cast<int>(i));
      batches_seen_for_fragment[fragment_index]++;
      if (static_cast<int>(i) == last_indices[fragment_index]) {
        dataset_->FinishFragment(fragment_index);
      }
      if (current_fragment_index == fragment_index) {
        if (seen_fragment) {
          EXPECT_FINISHES_OK_AND_ASSIGN(auto next, gen());
          collected.push_back(std::move(next));
        } else {
          seen_fragment = true;
        }
        if (static_cast<int>(i) == last_indices[fragment_index]) {
          // Immediately collect your bonus fragment
          EXPECT_FINISHES_OK_AND_ASSIGN(auto next, gen());
          collected.push_back(std::move(next));
          // Now collect any batches freed up that couldn't be delivered because they came
          // from the wrong fragment
          auto last_fragment_index = fragment_index;
          fragment_index++;
          seen_fragment = batches_seen_for_fragment[fragment_index] > 0;
          while (fragment_index < num_fragments &&
                 fragment_index != last_fragment_index) {
            last_fragment_index = fragment_index;
            for (int j = 0; j < batches_seen_for_fragment[fragment_index] - 1; j++) {
              EXPECT_FINISHES_OK_AND_ASSIGN(auto next, gen());
              collected.push_back(std::move(next));
            }
            if (static_cast<int>(i) >= last_indices[fragment_index]) {
              EXPECT_FINISHES_OK_AND_ASSIGN(auto next, gen());
              collected.push_back(std::move(next));
              fragment_index++;
              if (fragment_index < num_fragments) {
                seen_fragment = batches_seen_for_fragment[fragment_index] > 0;
              }
            }
          }
        }
      }
    }
    return collected;
  }

  struct FragmentStats {
    int last_index;
    bool seen;
  };

  std::vector<FragmentStats> GetFragmentStats(const std::vector<int>& order) {
    auto last_indices = GetLastIndices(order);
    std::vector<FragmentStats> fragment_stats;
    for (std::size_t i = 0; i < last_indices.size(); i++) {
      fragment_stats.push_back({last_indices[i], false});
    }
    return fragment_stats;
  }

  /// When data arrives out of order then we first have to buffer up 1 item in order to
  /// know when the last item has arrived (so we can mark it as the last).  This means
  /// sometimes we deliver an item and don't get one (first in a fragment) and sometimes
  /// we deliver an item and we end up getting two (last in a fragment)
  std::vector<EnumeratedRecordBatch> DeliverAndCollect(
      std::vector<int> order, EnumeratedRecordBatchGenerator gen) {
    std::vector<EnumeratedRecordBatch> collected;
    auto fragment_stats = GetFragmentStats(order);
    for (std::size_t i = 0; i < order.size(); i++) {
      auto fragment_index = order[i];
      dataset_->DeliverBatch(fragment_index, static_cast<int>(i));
      if (static_cast<int>(i) == fragment_stats[fragment_index].last_index) {
        dataset_->FinishFragment(fragment_index);
        EXPECT_FINISHES_OK_AND_ASSIGN(auto next, gen());
        collected.push_back(std::move(next));
      }
      if (!fragment_stats[fragment_index].seen) {
        fragment_stats[fragment_index].seen = true;
      } else {
        EXPECT_FINISHES_OK_AND_ASSIGN(auto next, gen());
        collected.push_back(std::move(next));
      }
    }
    return collected;
  }

  std::shared_ptr<Scanner> MakeScanner(int fragment_readahead = 0) {
    ScannerBuilder builder(dataset_);
    // Reordering tests only make sense for async
    ARROW_EXPECT_OK(builder.UseAsync(true));
    if (fragment_readahead != 0) {
      ARROW_EXPECT_OK(builder.FragmentReadahead(fragment_readahead));
    }
    EXPECT_OK_AND_ASSIGN(auto scanner, builder.Finish());
    return scanner;
  }

  void AssertBatchesInOrder(const std::vector<TaggedRecordBatch>& batches,
                            std::vector<int> expected_order) {
    ASSERT_EQ(expected_order.size(), batches.size());
    for (std::size_t i = 0; i < batches.size(); i++) {
      ASSERT_EQ(expected_order[i], batches[i].record_batch->num_rows());
    }
  }

  void AssertBatchesInOrder(const std::vector<EnumeratedRecordBatch>& batches,
                            std::vector<int> expected_batch_indices,
                            std::vector<int> expected_row_sizes) {
    ASSERT_EQ(expected_batch_indices.size(), batches.size());
    for (std::size_t i = 0; i < batches.size(); i++) {
      ASSERT_EQ(expected_row_sizes[i], batches[i].record_batch.value->num_rows());
      ASSERT_EQ(expected_batch_indices[i], batches[i].record_batch.index);
    }
  }

  std::shared_ptr<ControlledDataset> dataset_;
};

TEST_F(TestReordering, ScanBatches) {
  auto scanner = MakeScanner();
  ASSERT_OK_AND_ASSIGN(auto batch_gen, scanner->ScanBatchesAsync());
  auto collected = DeliverAndCollect({0, 0, 1, 1, 0}, std::move(batch_gen));
  AssertBatchesInOrder(collected, {0, 1, 4, 2, 3});
}

TEST_F(TestReordering, ScanBatchesUnordered) {
  auto scanner = MakeScanner();
  ASSERT_OK_AND_ASSIGN(auto batch_gen, scanner->ScanBatchesUnorderedAsync());
  auto collected = DeliverAndCollect({0, 0, 1, 1, 0}, std::move(batch_gen));
  AssertBatchesInOrder(collected, {0, 0, 1, 1, 2}, {0, 2, 3, 1, 4});
}

struct BatchConsumer {
  explicit BatchConsumer(EnumeratedRecordBatchGenerator generator)
      : generator(generator), next() {}

  void AssertCanConsume() {
    if (!next.is_valid()) {
      next = generator();
    }
    ASSERT_FINISHES_OK(next);
    next = Future<EnumeratedRecordBatch>();
  }

  void AssertCannotConsume() {
    if (!next.is_valid()) {
      next = generator();
    }
    SleepABit();
    ASSERT_FALSE(next.is_finished());
  }

  void AssertFinished() {
    if (!next.is_valid()) {
      next = generator();
    }
    ASSERT_FINISHES_OK_AND_ASSIGN(auto last, next);
    ASSERT_TRUE(IsIterationEnd(last));
  }

  EnumeratedRecordBatchGenerator generator;
  Future<EnumeratedRecordBatch> next;
};

TEST_F(TestReordering, FileReadahead) {
  auto scanner = MakeScanner(/*fragment_readahead=*/1);
  ASSERT_OK_AND_ASSIGN(auto batch_gen, scanner->ScanBatchesUnorderedAsync());
  BatchConsumer consumer(std::move(batch_gen));
  dataset_->DeliverBatch(0, 0);
  dataset_->DeliverBatch(0, 1);
  consumer.AssertCanConsume();
  consumer.AssertCannotConsume();
  dataset_->DeliverBatch(1, 0);
  consumer.AssertCannotConsume();
  dataset_->FinishFragment(1);
  // Even though fragment 1 is finished we cannot read it because fragment_readahead
  // is 1 so we should only be reading fragment 0
  consumer.AssertCannotConsume();
  dataset_->FinishFragment(0);
  consumer.AssertCanConsume();
  consumer.AssertCanConsume();
  consumer.AssertFinished();
}

class TestScannerBuilder : public ::testing::Test {
  void SetUp() override {
    DatasetVector sources;

    schema_ = schema({
        field("b", boolean()),
        field("i8", int8()),
        field("i16", int16()),
        field("i32", int32()),
        field("i64", int64()),
    });

    ASSERT_OK_AND_ASSIGN(dataset_, UnionDataset::Make(schema_, sources));
  }

 protected:
  std::shared_ptr<ScanOptions> options_ = std::make_shared<ScanOptions>();
  std::shared_ptr<Schema> schema_;
  std::shared_ptr<Dataset> dataset_;
};

TEST_F(TestScannerBuilder, TestProject) {
  ScannerBuilder builder(dataset_, options_);

  // It is valid to request no columns, e.g. `SELECT 1 FROM t WHERE t.a > 0`.
  // still needs to touch the `a` column.
  ASSERT_OK(builder.Project({}));
  ASSERT_OK(builder.Project({"i64", "b", "i8"}));
  ASSERT_OK(builder.Project({"i16", "i16"}));
  ASSERT_OK(builder.Project(
      {field_ref("i16"), call("multiply", {field_ref("i16"), literal(2)})},
      {"i16 renamed", "i16 * 2"}));

  ASSERT_RAISES(Invalid, builder.Project({"not_found_column"}));
  ASSERT_RAISES(Invalid, builder.Project({"i8", "not_found_column"}));
  ASSERT_RAISES(Invalid,
                builder.Project({field_ref("not_found_column"),
                                 call("multiply", {field_ref("i16"), literal(2)})},
                                {"i16 renamed", "i16 * 2"}));

  ASSERT_RAISES(NotImplemented, builder.Project({field_ref(FieldRef("nested", "column"))},
                                                {"nested column"}));

  // provided more field names than column exprs or vice versa
  ASSERT_RAISES(Invalid, builder.Project({}, {"i16 renamed", "i16 * 2"}));
  ASSERT_RAISES(Invalid, builder.Project({literal(2), field_ref("a")}, {"a"}));
}

TEST_F(TestScannerBuilder, TestFilter) {
  ScannerBuilder builder(dataset_, options_);

  ASSERT_OK(builder.Filter(literal(true)));
  ASSERT_OK(builder.Filter(equal(field_ref("i64"), literal<int64_t>(10))));
  ASSERT_OK(builder.Filter(or_(equal(field_ref("i64"), literal<int64_t>(10)),
                               equal(field_ref("b"), literal(true)))));

  ASSERT_OK(builder.Filter(equal(field_ref("i64"), literal<double>(10))));

  ASSERT_RAISES(Invalid, builder.Filter(equal(field_ref("not_a_column"), literal(true))));

  ASSERT_RAISES(
      NotImplemented,
      builder.Filter(equal(field_ref(FieldRef("nested", "column")), literal(true))));

  ASSERT_RAISES(Invalid,
                builder.Filter(or_(equal(field_ref("i64"), literal<int64_t>(10)),
                                   equal(field_ref("not_a_column"), literal(true)))));
}

TEST(ScanOptions, TestMaterializedFields) {
  auto i32 = field("i32", int32());
  auto i64 = field("i64", int64());
  auto opts = std::make_shared<ScanOptions>();

  // empty dataset, project nothing = nothing materialized
  opts->dataset_schema = schema({});
  ASSERT_OK(SetProjection(opts.get(), {}, {}));
  EXPECT_THAT(opts->MaterializedFields(), IsEmpty());

  // non-empty dataset, project nothing = nothing materialized
  opts->dataset_schema = schema({i32, i64});
  EXPECT_THAT(opts->MaterializedFields(), IsEmpty());

  // project nothing, filter on i32 = materialize i32
  opts->filter = equal(field_ref("i32"), literal(10));
  EXPECT_THAT(opts->MaterializedFields(), ElementsAre("i32"));

  // project i32 & i64, filter nothing = materialize i32 & i64
  opts->filter = literal(true);
  ASSERT_OK(SetProjection(opts.get(), {"i32", "i64"}));
  EXPECT_THAT(opts->MaterializedFields(), ElementsAre("i32", "i64"));

  // project i32 + i64, filter nothing = materialize i32 & i64
  opts->filter = literal(true);
  ASSERT_OK(SetProjection(opts.get(), {call("add", {field_ref("i32"), field_ref("i64")})},
                          {"i32 + i64"}));
  EXPECT_THAT(opts->MaterializedFields(), ElementsAre("i32", "i64"));

  // project i32, filter nothing = materialize i32
  ASSERT_OK(SetProjection(opts.get(), {"i32"}));
  EXPECT_THAT(opts->MaterializedFields(), ElementsAre("i32"));

  // project i32, filter on i32 = materialize i32 (reported twice)
  opts->filter = equal(field_ref("i32"), literal(10));
  EXPECT_THAT(opts->MaterializedFields(), ElementsAre("i32", "i32"));

  // project i32, filter on i32 & i64 = materialize i64, i32 (reported twice)
  opts->filter = less(field_ref("i32"), field_ref("i64"));
  EXPECT_THAT(opts->MaterializedFields(), ElementsAre("i32", "i64", "i32"));

  // project i32, filter on i64 = materialize i32 & i64
  opts->filter = equal(field_ref("i64"), literal(10));
  EXPECT_THAT(opts->MaterializedFields(), ElementsAre("i64", "i32"));
}

}  // namespace dataset
}  // namespace arrow
