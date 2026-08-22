// SPDX-License-Identifier: Apache-2.0

package main

import (
	"bufio"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"time"

	volumeutil "k8s.io/kubernetes/pkg/volume/util"
)

const fixedPaths = 4

type phaseTimes struct {
	SetupNS            int64 `json:"setup_ns"`
	InitialPublishNS   int64 `json:"initial_publish_ns"`
	InitialConsumerNS  int64 `json:"initial_consumer_ns"`
	UpdatePublishNS    int64 `json:"update_publish_ns"`
	UpdateConsumerNS   int64 `json:"update_consumer_ns"`
	NoOpPublishNS      int64 `json:"no_op_publish_ns"`
	NoOpConsumerNS     int64 `json:"no_op_consumer_ns"`
	RollbackPublishNS  int64 `json:"rollback_publish_ns"`
	RollbackConsumerNS int64 `json:"rollback_consumer_ns"`
}

type consumerAck struct {
	State              string         `json:"state"`
	Pass               bool           `json:"pass"`
	Error              int            `json:"error"`
	ReaddirOps         int            `json:"readdir_ops"`
	OpenOps            int            `json:"open_ops"`
	ReadOps            int            `json:"read_ops"`
	StatOps            int            `json:"stat_ops"`
	MissingOps         int            `json:"missing_ops"`
	OldFDOps           int            `json:"old_fd_ops"`
	VisibleRootEntries int            `json:"visible_root_entries"`
	RootDev            uint64         `json:"root_dev"`
	RootIno            uint64         `json:"root_ino"`
	AppDev             uint64         `json:"app_dev"`
	AppIno             uint64         `json:"app_ino"`
	OldDev             uint64         `json:"old_dev"`
	OldIno             uint64         `json:"old_ino"`
	OldBytes           string         `json:"old_bytes"`
	OldError           int            `json:"old_error"`
	OldMode            uint32         `json:"old_mode"`
	OldUID             uint32         `json:"old_uid"`
	OldGID             uint32         `json:"old_gid"`
	OldSize            int64          `json:"old_size"`
	Files              []observedFile `json:"files"`
	RootEntries        []string       `json:"root_entries"`
	ConfigEntries      []string       `json:"config_entries"`
	TLSEntries         []string       `json:"tls_entries"`
}

type timedAck struct {
	State string `json:"state"`
	Pass  bool   `json:"pass"`
	Error int    `json:"error"`
}

type observedFile struct {
	Path  string `json:"path"`
	Bytes string `json:"bytes"`
	Error int    `json:"error"`
	Mode  uint32 `json:"mode"`
	UID   uint32 `json:"uid"`
	GID   uint32 `json:"gid"`
	Dev   uint64 `json:"dev"`
	Ino   uint64 `json:"ino"`
	Size  int64  `json:"size"`
}

type materializationObservation struct {
	State                  string `json:"state"`
	AuditLifecycle         bool   `json:"audit_lifecycle"`
	DataTarget             string `json:"data_target"`
	Changed                bool   `json:"changed"`
	LiveRegularFiles       int    `json:"live_regular_files"`
	LivePayloadBytes       int64  `json:"live_payload_bytes"`
	NewlyMaterializedFiles int    `json:"newly_materialized_files"`
	NewlyMaterializedBytes int64  `json:"newly_materialized_bytes"`
}

type lifecycleObservation struct {
	Event                  string        `json:"event"`
	Mechanism              string        `json:"mechanism"`
	Boot                   int           `json:"boot"`
	Pair                   int           `json:"pair"`
	Order                  int           `json:"order"`
	Width                  int           `json:"width"`
	PresentPerState        int           `json:"present_per_state"`
	ChangedUnionPaths      int           `json:"changed_union_paths"`
	ActiveTotalNS          int64         `json:"active_total_ns"`
	WallSpanNS             int64         `json:"wall_span_ns"`
	PublicationOnlyNS      int64         `json:"publication_only_ns"`
	ConsumerOnlyNS         int64         `json:"consumer_only_ns"`
	Phases                 phaseTimes    `json:"phases"`
	Consumer               []consumerAck `json:"consumer"`
	RuntimeUID             uint32        `json:"runtime_uid"`
	RuntimeGID             uint32        `json:"runtime_gid"`
	ConsumerExitStatus     int           `json:"consumer_exit_status"`
	CleanupRootAbsent      bool          `json:"cleanup_root_absent"`
	CleanupParentEntries   []string      `json:"cleanup_parent_entries"`
	CleanupRootRemoveError int           `json:"cleanup_root_remove_error"`
	CleanupRootStatError   int           `json:"cleanup_root_stat_error"`
	CleanupParentReadError int           `json:"cleanup_parent_read_error"`
	CleanupPass            bool          `json:"cleanup_pass"`
	Error                  string        `json:"error,omitempty"`
	Pass                   bool          `json:"pass"`
}

type materializationAudit struct {
	Event                  string                       `json:"event"`
	Mechanism              string                       `json:"mechanism"`
	Boot                   int                          `json:"boot"`
	Pair                   int                          `json:"pair"`
	Width                  int                          `json:"width"`
	Materialization        []materializationObservation `json:"materialization"`
	RuntimeUID             uint32                       `json:"runtime_uid"`
	RuntimeGID             uint32                       `json:"runtime_gid"`
	CleanupRootAbsent      bool                         `json:"cleanup_root_absent"`
	CleanupParentEntries   []string                     `json:"cleanup_parent_entries"`
	CleanupRootRemoveError int                          `json:"cleanup_root_remove_error"`
	CleanupRootStatError   int                          `json:"cleanup_root_stat_error"`
	CleanupParentReadError int                          `json:"cleanup_parent_read_error"`
	CleanupPass            bool                         `json:"cleanup_pass"`
	Error                  string                       `json:"error,omitempty"`
	Pass                   bool                         `json:"pass"`
}

func errnoNumber(err error) int {
	if err == nil {
		return 0
	}
	var value syscall.Errno
	if errors.As(err, &value) {
		return int(value)
	}
	return -1
}

func payloads(width int) (map[string]volumeutil.FileProjection,
	map[string]volumeutil.FileProjection) {
	v0 := map[string]volumeutil.FileProjection{
		"config/app.conf": {Data: []byte("version=0\n"), Mode: 0644},
		"tls/cert.pem":    {Data: []byte("certificate-v0\n"), Mode: 0400},
		"retired.conf":    {Data: []byte("retired\n"), Mode: 0644},
	}
	v1 := map[string]volumeutil.FileProjection{
		"config/app.conf": {Data: []byte("version=1\n"), Mode: 0600},
		"tls/cert.pem":    {Data: []byte("certificate-v1\n"), Mode: 0400},
		"added.conf":      {Data: []byte("added\n"), Mode: 0644},
	}
	for index := 0; index < width-fixedPaths; index++ {
		path := fmt.Sprintf("entry-%03d.conf", index)
		data := []byte(fmt.Sprintf("stable-%03d\n", index))
		projection := volumeutil.FileProjection{Data: data, Mode: 0644}
		v0[path] = projection
		v1[path] = projection
	}
	return v0, v1
}

func writePayload(writer *volumeutil.AtomicWriter,
	payload map[string]volumeutil.FileProjection,
	setPerms func(string) error) (int64, error) {
	start := time.Now()
	err := writer.Write(payload, setPerms)
	return time.Since(start).Nanoseconds(), err
}

func sendState(input *bufio.Writer, output *bufio.Scanner,
	state string) (int64, error) {
	var ack timedAck
	start := time.Now()
	if _, err := fmt.Fprintln(input, state); err != nil {
		return 0, err
	}
	if err := input.Flush(); err != nil {
		return 0, err
	}
	if !output.Scan() {
		if err := output.Err(); err != nil {
			return 0, err
		}
		return 0, fmt.Errorf("consumer exited before %s acknowledgement", state)
	}
	elapsed := time.Since(start).Nanoseconds()
	if err := json.Unmarshal(output.Bytes(), &ack); err != nil {
		return 0, err
	}
	if ack.State != state || !ack.Pass || ack.Error != 0 {
		return 0, fmt.Errorf("consumer rejected %s: %+v", state, ack)
	}
	return elapsed, nil
}

func collectEvidence(input *bufio.Writer, output *bufio.Scanner) (
	[]consumerAck, error) {
	states := []string{"initial", "update", "no-op", "rollback"}
	if _, err := fmt.Fprintln(input, "evidence"); err != nil {
		return nil, err
	}
	if err := input.Flush(); err != nil {
		return nil, err
	}
	observations := make([]consumerAck, 0, len(states))
	for _, state := range states {
		if !output.Scan() {
			if err := output.Err(); err != nil {
				return nil, err
			}
			return nil, fmt.Errorf("consumer exited before %s evidence", state)
		}
		var observation consumerAck
		if err := json.Unmarshal(output.Bytes(), &observation); err != nil {
			return nil, err
		}
		if observation.State != state || !observation.Pass || observation.Error != 0 {
			return nil, fmt.Errorf("consumer evidence rejected %s: %+v",
				state, observation)
		}
		observations = append(observations, observation)
	}
	return observations, nil
}

func captureMaterialization(root, state, previousTarget string) (
	materializationObservation, error) {
	observation := materializationObservation{State: state}
	target, err := os.Readlink(filepath.Join(root, "..data"))
	if err != nil {
		return observation, err
	}
	observation.DataTarget = target
	observation.Changed = target != previousTarget
	err = filepath.Walk(filepath.Join(root, target),
		func(_ string, info os.FileInfo, walkErr error) error {
			if walkErr != nil {
				return walkErr
			}
			if info.Mode().IsRegular() {
				observation.LiveRegularFiles++
				observation.LivePayloadBytes += info.Size()
			}
			return nil
		})
	if err != nil {
		return observation, err
	}
	if observation.Changed {
		observation.NewlyMaterializedFiles = observation.LiveRegularFiles
		observation.NewlyMaterializedBytes = observation.LivePayloadBytes
	}
	return observation, nil
}

func auditMaterializations(root string, uid, gid int,
	v0, v1 map[string]volumeutil.FileProjection) (
	[]materializationObservation, error) {
	if err := os.Mkdir(root, 0755); err != nil {
		return nil, err
	}
	writer, err := volumeutil.NewAtomicWriter(root, "namei-ext-quantitative-audit")
	if err != nil {
		return nil, err
	}
	setPerms := func(subPath string) error {
		return filepath.Walk(filepath.Join(root, subPath),
			func(path string, _ os.FileInfo, walkErr error) error {
				if walkErr != nil {
					return walkErr
				}
				return os.Chown(path, uid, gid)
			})
	}
	states := []string{"initial", "update", "no-op", "rollback"}
	payloadSequence := []map[string]volumeutil.FileProjection{v0, v1, v1, v0}
	previousTarget := ""
	observations := make([]materializationObservation, 0, len(states))
	for index, state := range states {
		if _, err := writePayload(writer, payloadSequence[index], setPerms); err != nil {
			return nil, err
		}
		observation, err := captureMaterialization(root, state, previousTarget)
		if err != nil {
			return nil, err
		}
		observation.AuditLifecycle = true
		observations = append(observations, observation)
		previousTarget = observation.DataTarget
	}
	return observations, nil
}

func appendObservation(path string, observation any) error {
	result, err := os.OpenFile(path, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
	if err != nil {
		return err
	}
	encodeErr := json.NewEncoder(result).Encode(observation)
	closeErr := result.Close()
	if encodeErr != nil {
		return encodeErr
	}
	return closeErr
}

func run(outputPath, consumerPath, parent string, width, boot, pair, order int) (
	runErr error) {
	parentInfo, err := os.Stat(parent)
	if err != nil {
		return err
	}
	parentStat, ok := parentInfo.Sys().(*syscall.Stat_t)
	if !ok {
		return fmt.Errorf("stat payload unavailable for %s", parent)
	}
	uid := int(parentStat.Uid)
	gid := int(parentStat.Gid)
	if uid == 0 || gid == 0 {
		return fmt.Errorf("sample parent must have a non-root owner")
	}
	root := filepath.Join(parent, "atomicwriter")
	if _, err := os.Stat(root); !os.IsNotExist(err) {
		return fmt.Errorf("condition root must not exist before timing: %s", root)
	}
	v0, v1 := payloads(width)
	command := exec.Command(consumerPath, root, "atomicwriter",
		strconv.Itoa(width), strconv.Itoa(uid), strconv.Itoa(gid))
	consumerInput, err := command.StdinPipe()
	if err != nil {
		return err
	}
	consumerOutput, err := command.StdoutPipe()
	if err != nil {
		return err
	}
	command.Stderr = os.Stderr
	if err := command.Start(); err != nil {
		return err
	}
	input := bufio.NewWriter(consumerInput)
	output := bufio.NewScanner(consumerOutput)
	output.Buffer(make([]byte, 4096), 1024*1024)
	inputClosed := false
	consumerWaited := false
	observation := lifecycleObservation{
		Event:                "kubernetes-configmap-quantitative-lifecycle",
		Mechanism:            "atomicwriter",
		Boot:                 boot,
		Pair:                 pair,
		Order:                order,
		Width:                width,
		PresentPerState:      width - 1,
		ChangedUnionPaths:    fixedPaths,
		RuntimeUID:           uint32(uid),
		RuntimeGID:           uint32(gid),
		ConsumerExitStatus:   -1,
		CleanupParentEntries: []string{},
	}
	defer func() {
		cleanupPass := true
		if !inputClosed {
			if _, cleanupErr := fmt.Fprintln(input, "quit"); cleanupErr == nil {
				cleanupErr = input.Flush()
			}
			if cleanupErr := consumerInput.Close(); cleanupErr != nil && runErr == nil {
				runErr = cleanupErr
			}
			inputClosed = true
		}
		if !consumerWaited {
			cleanupErr := command.Wait()
			if command.ProcessState != nil {
				observation.ConsumerExitStatus = command.ProcessState.ExitCode()
			}
			if cleanupErr != nil && runErr == nil {
				runErr = cleanupErr
			}
			consumerWaited = true
		}
		cleanupErr := os.RemoveAll(root)
		observation.CleanupRootRemoveError = errnoNumber(cleanupErr)
		if cleanupErr != nil {
			cleanupPass = false
			if runErr == nil {
				runErr = cleanupErr
			}
		}
		_, cleanupErr = os.Stat(root)
		observation.CleanupRootStatError = errnoNumber(cleanupErr)
		if os.IsNotExist(cleanupErr) {
			observation.CleanupRootAbsent = true
		} else {
			cleanupPass = false
			if runErr == nil {
				runErr = fmt.Errorf("source root remains after cleanup")
			}
		}
		entries, cleanupErr := os.ReadDir(parent)
		observation.CleanupParentReadError = errnoNumber(cleanupErr)
		if cleanupErr != nil {
			cleanupPass = false
			if runErr == nil {
				runErr = cleanupErr
			}
		} else {
			names := make([]string, 0, len(entries))
			for _, entry := range entries {
				names = append(names, entry.Name())
			}
			observation.CleanupParentEntries = names
			if len(entries) != 0 {
				cleanupPass = false
			}
			if len(entries) != 0 && runErr == nil {
				runErr = fmt.Errorf("source cleanup left entries: %s",
					strings.Join(names, ","))
			}
		}
		observation.CleanupPass = cleanupPass
		observation.Pass = runErr == nil && cleanupPass &&
			len(observation.Consumer) == 4
		if runErr != nil {
			observation.Error = runErr.Error()
		}
		if encodeErr := appendObservation(outputPath, observation); encodeErr != nil &&
			runErr == nil {
			runErr = encodeErr
		}
	}()
	wallStart := time.Now()
	setupStart := time.Now()
	if err := os.Mkdir(root, 0755); err != nil {
		return err
	}
	writer, err := volumeutil.NewAtomicWriter(root, "namei-ext-quantitative")
	observation.Phases.SetupNS = time.Since(setupStart).Nanoseconds()
	if err != nil {
		return err
	}
	setPerms := func(subPath string) error {
		return filepath.Walk(filepath.Join(root, subPath),
			func(path string, _ os.FileInfo, walkErr error) error {
				if walkErr != nil {
					return walkErr
				}
				return os.Chown(path, uid, gid)
			})
	}
	if observation.Phases.InitialPublishNS, err = writePayload(writer, v0, setPerms); err != nil {
		return err
	}
	elapsed, err := sendState(input, output, "initial")
	if err != nil {
		return err
	}
	observation.Phases.InitialConsumerNS = elapsed
	if observation.Phases.UpdatePublishNS, err = writePayload(writer, v1, setPerms); err != nil {
		return err
	}
	elapsed, err = sendState(input, output, "update")
	if err != nil {
		return err
	}
	observation.Phases.UpdateConsumerNS = elapsed
	if observation.Phases.NoOpPublishNS, err = writePayload(writer, v1, setPerms); err != nil {
		return err
	}
	elapsed, err = sendState(input, output, "no-op")
	if err != nil {
		return err
	}
	observation.Phases.NoOpConsumerNS = elapsed
	if observation.Phases.RollbackPublishNS, err = writePayload(writer, v0, setPerms); err != nil {
		return err
	}
	elapsed, err = sendState(input, output, "rollback")
	if err != nil {
		return err
	}
	observation.Phases.RollbackConsumerNS = elapsed
	observation.WallSpanNS = time.Since(wallStart).Nanoseconds()
	observation.PublicationOnlyNS = observation.Phases.InitialPublishNS +
		observation.Phases.UpdatePublishNS + observation.Phases.NoOpPublishNS +
		observation.Phases.RollbackPublishNS
	observation.ConsumerOnlyNS = observation.Phases.InitialConsumerNS +
		observation.Phases.UpdateConsumerNS + observation.Phases.NoOpConsumerNS +
		observation.Phases.RollbackConsumerNS
	observation.ActiveTotalNS = observation.Phases.SetupNS +
		observation.PublicationOnlyNS + observation.ConsumerOnlyNS
	observation.Consumer, err = collectEvidence(input, output)
	if err != nil {
		return err
	}
	if _, err := fmt.Fprintln(input, "quit"); err != nil {
		return err
	}
	if err := input.Flush(); err != nil {
		return err
	}
	if err := consumerInput.Close(); err != nil {
		return err
	}
	inputClosed = true
	if err := command.Wait(); err != nil {
		return err
	}
	observation.ConsumerExitStatus = command.ProcessState.ExitCode()
	consumerWaited = true
	return nil
}

func runAudit(outputPath, parent string, width, boot, pair int) (runErr error) {
	parentInfo, err := os.Stat(parent)
	if err != nil {
		return err
	}
	parentStat, ok := parentInfo.Sys().(*syscall.Stat_t)
	if !ok {
		return fmt.Errorf("stat payload unavailable for %s", parent)
	}
	uid := int(parentStat.Uid)
	gid := int(parentStat.Gid)
	if uid == 0 || gid == 0 {
		return fmt.Errorf("audit parent must have a non-root owner")
	}
	root := filepath.Join(parent, "atomicwriter-audit")
	if _, err := os.Stat(root); !os.IsNotExist(err) {
		return fmt.Errorf("audit root must not exist: %s", root)
	}
	v0, v1 := payloads(width)
	observation := materializationAudit{
		Event:                "kubernetes-configmap-quantitative-materialization-audit",
		Mechanism:            "atomicwriter",
		Boot:                 boot,
		Pair:                 pair,
		Width:                width,
		RuntimeUID:           uint32(uid),
		RuntimeGID:           uint32(gid),
		CleanupParentEntries: []string{},
	}
	defer func() {
		cleanupPass := true
		cleanupErr := os.RemoveAll(root)
		observation.CleanupRootRemoveError = errnoNumber(cleanupErr)
		if cleanupErr != nil {
			cleanupPass = false
			if runErr == nil {
				runErr = cleanupErr
			}
		}
		_, cleanupErr = os.Stat(root)
		observation.CleanupRootStatError = errnoNumber(cleanupErr)
		if os.IsNotExist(cleanupErr) {
			observation.CleanupRootAbsent = true
		} else {
			cleanupPass = false
			if runErr == nil {
				runErr = fmt.Errorf("audit root remains after cleanup")
			}
		}
		entries, cleanupErr := os.ReadDir(parent)
		observation.CleanupParentReadError = errnoNumber(cleanupErr)
		if cleanupErr != nil {
			cleanupPass = false
			if runErr == nil {
				runErr = cleanupErr
			}
		} else {
			names := make([]string, 0, len(entries))
			for _, entry := range entries {
				names = append(names, entry.Name())
			}
			observation.CleanupParentEntries = names
			if len(entries) != 0 {
				cleanupPass = false
				if runErr == nil {
					runErr = fmt.Errorf("audit cleanup left entries: %s",
						strings.Join(names, ","))
				}
			}
		}
		observation.CleanupPass = cleanupPass
		observation.Pass = runErr == nil && cleanupPass &&
			len(observation.Materialization) == 4
		if runErr != nil {
			observation.Error = runErr.Error()
		}
		if encodeErr := appendObservation(outputPath, observation); encodeErr != nil &&
			runErr == nil {
			runErr = encodeErr
		}
	}()
	observation.Materialization, runErr = auditMaterializations(
		root, uid, gid, v0, v1)
	return runErr
}

func parsePositive(name, value string) (int, error) {
	parsed, err := strconv.Atoi(value)
	if err != nil || parsed <= 0 {
		return 0, fmt.Errorf("%s must be a positive integer", name)
	}
	return parsed, nil
}

func main() {
	if len(os.Args) < 2 {
		fmt.Fprintf(os.Stderr,
			"usage: %s timed OUTPUT CONSUMER PARENT WIDTH BOOT PAIR ORDER | audit OUTPUT PARENT WIDTH BOOT PAIR\n",
			os.Args[0])
		os.Exit(2)
	}
	mode := os.Args[1]
	if (mode == "timed" && len(os.Args) != 9) ||
		(mode == "audit" && len(os.Args) != 7) ||
		(mode != "timed" && mode != "audit") {
		fmt.Fprintln(os.Stderr, "invalid mode or argument count")
		os.Exit(2)
	}
	widthArgument := 5
	bootArgument := 6
	pairArgument := 7
	if mode == "audit" {
		widthArgument = 4
		bootArgument = 5
		pairArgument = 6
	}
	width, err := parsePositive("width", os.Args[widthArgument])
	if err == nil && (width < fixedPaths || width > 256) {
		err = fmt.Errorf("width must be between %d and 256", fixedPaths)
	}
	boot := 0
	pair := 0
	order := 0
	if err == nil {
		boot, err = parsePositive("boot", os.Args[bootArgument])
	}
	if err == nil {
		pair, err = parsePositive("pair", os.Args[pairArgument])
	}
	if err == nil && mode == "timed" {
		order, err = parsePositive("order", os.Args[8])
	}
	if err == nil && order > 2 {
		err = fmt.Errorf("order must be 1 or 2")
	}
	if err == nil {
		if mode == "timed" {
			err = run(os.Args[2], os.Args[3], os.Args[4], width,
				boot, pair, order)
		} else {
			err = runAudit(os.Args[2], os.Args[3], width, boot, pair)
		}
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
