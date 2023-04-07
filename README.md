# SnapshotUpdater
Snapshot Updater is an Android command-line tool that updates snapshots post-OTA.

## Usage

```bash
shapshotupdater update <snapshot-name> <snapshot-size>
```

The snapshot is resized to the requested size. If size increases, the snapshot occupying the smallest amount of space
on `super` large enough to accommodate the change will be partially offloaded to disk. A temporary backup of the entire
snapshot will be temporarily stored on disk during the operation.
