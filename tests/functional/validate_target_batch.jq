def absolute:
  if . < 0 then -. else . end;

def arm($pair; $mechanism):
  [.[] |
   select(.event == "target-registration-diagnostic" and
          .pair == $pair and .mechanism == $mechanism)] |
  if length == 1 then .[0]
  else error("target-registration arm coordinate is not unique")
  end;

def pair_row($pair):
  [.[] |
   select(.event == "target-registration-pair" and .pair == $pair)] |
  if length == 1 then .[0]
  else error("target-registration pair coordinate is not unique")
  end;

def required_case($name):
  [.[] | select(.event == "case" and .name == $name)] |
  length == 1 and .[0].pass == true;

. as $rows |
([.[] | select(.event == "target-registration-diagnostic")] | length) == 10 and
all(.[] | select(.event == "target-registration-diagnostic");
    .targets == 64 and .elapsed_ns > 0 and .register_error == 0 and
    .clear_error == 0 and .cgroup_remove_error == 0 and .pass == true) and
all(range(1; 6);
    . as $pair |
    ($rows | arm($pair; "scalar")) as $scalar |
    ($rows | arm($pair; "batch")) as $batch |
    ($rows | pair_row($pair)) as $paired |
    $scalar.order == (if ($pair % 2) == 1 then 1 else 2 end) and
    $batch.order == (if ($pair % 2) == 1 then 2 else 1 end) and
    $paired.first == (if ($pair % 2) == 1 then "scalar" else "batch" end) and
    $paired.targets == 64 and $paired.scalar_ns == $scalar.elapsed_ns and
    $paired.batch_ns == $batch.elapsed_ns and $paired.pass == true and
    (($paired.batch_over_scalar -
      ($batch.elapsed_ns / $scalar.elapsed_ns)) | absolute) <= 0.000000001) and
([range(1; 6) as $pair |
  ($rows | arm($pair; "scalar")) as $scalar |
  ($rows | arm($pair; "batch")) as $batch |
  $batch.elapsed_ns / $scalar.elapsed_ns] | sort | .[2]) as $median |
([range(1; 6) as $pair |
  ($rows | arm($pair; "scalar")) as $scalar |
  ($rows | arm($pair; "batch")) as $batch |
  select($batch.elapsed_ns < $scalar.elapsed_ns)] | length) as $wins |
([.[] | select(.event == "target-registration-diagnostic-summary")] |
 if length == 1 then .[0]
 else error("target-registration summary is not unique")
 end) as $summary |
$summary.pairs == 5 and $summary.targets == 64 and
$summary.batch_wins == $wins and $summary.median_batch_over_scalar < 1 and
(($summary.median_batch_over_scalar - $median) | absolute) <= 0.000000001 and
$summary.pass == true and
all([
  "target_batch_partial_failure",
  "target_batch_partial_clear",
  "target_batch_multi_success",
  "target_batch_multi_clear",
  "target_batch_id_reuse",
  "target-batch-partial-prefix-directory",
  "target-batch-partial-prefix-file",
  "target-batch-partial-prefix-exec",
  "target-batch-partial-prefix-pinned",
  "target-batch-partial-clear-directory",
  "target-batch-partial-clear-file",
  "target-batch-partial-clear-exec",
  "target-batch-partial-clear-pinned"
][]; . as $name | $rows | required_case($name)) and
([.[] |
  select(.event == "case" and
         ((.name | startswith("target_batch")) or
          (.name | startswith("target-batch"))) and .pass != true)] | length) == 0
