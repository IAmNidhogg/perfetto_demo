SELECT '--- processes ---' AS section;
SELECT pid, name FROM process WHERE pid > 0;

SELECT '--- threads ---' AS section;
SELECT thread.tid, thread.name AS thread_name, process.name AS process_name
FROM thread JOIN process USING(upid)
WHERE thread.tid > 0
ORDER BY thread.tid;

SELECT '--- slices (thread-scoped) ---' AS section;
SELECT thread.name AS thread, slice.name AS slice, slice.ts, slice.dur, slice.depth
FROM slice
JOIN thread_track ON slice.track_id = thread_track.id
JOIN thread USING(utid)
ORDER BY slice.ts;

SELECT '--- per-thread total slice duration ---' AS section;
SELECT thread.name AS thread, COUNT(*) AS n_slices, SUM(slice.dur) AS total_dur_ns
FROM slice
JOIN thread_track ON slice.track_id = thread_track.id
JOIN thread USING(utid)
WHERE slice.dur > 0
GROUP BY thread.name
ORDER BY total_dur_ns DESC;

SELECT '--- counter samples ---' AS section;
SELECT track.name AS track, counter.ts, counter.value
FROM counter
JOIN counter_track AS track ON counter.track_id = track.id
ORDER BY counter.ts;
