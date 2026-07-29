# Reading Stats Media Fixture

Duet's public screenshots use a deterministic, fabricated reading history. The fixture exists only in the simulator smoke-test path. It never reads either of Lauren's SD cards and it is not compiled into device firmware.

## Data Contract

The current simulator fixture includes:

- 360 days of varied reading activity, written oldest-to-newest.
- 237 local sessions, 209 hours 57 minutes, and 14,963 screen pages.
- A 15-day current streak and a 31-day longest streak.
- One fictional synced reader, producing 393 sessions and 302 hours 45 minutes across both devices.
- 24 detailed book histories spread across the preceding year.
- 18 in-progress books with authors, time, session counts, and estimated completion dates.
- A 312-book library: 198 unread, 17 in progress, and 97 finished.
- Four populated genres, four authors, eight heat categories, and 24 series.
- Populated monthly, yearly, weekday, time-of-day, session-length, timeline, fastest-read, start/finish, Reader DNA, and Wrapped views.

Recognizable books may appear as interface display text and cover art in the broader gallery. Their progress, reading time, pace, sessions, dates, and every other statistic are invented. All remaining fixture book and reader names are fictional.

## Capture Inventory

The gallery captures all 33 top-level Reading Stats tabs:

1. Current
2. Progress
3. Book
4. Device
5. Synced
6. Devices
7. Trends
8. Activity
9. 90 Days
10. Calendar
11. Heatmap
12. Profile
13. Goals
14. Sessions
15. Weekdays
16. Pace
17. Time of Day
18. Months
19. Year
20. Sessions Mix
21. Streaks
22. Start/Finish
23. Dates
24. Reader DNA
25. DNA Details
26. Signature
27. Signature Details
28. Fastest
29. Wrapped
30. Started
31. Library
32. Taste
33. Series

It also captures ten useful alternate and detail states:

- Current with the latest completed session instead of an active session.
- 90 Days scrolled.
- Day Details.
- Day Details in time-correction mode.
- Sessions scrolled.
- Reading Dates scrolled.
- Started scrolled.
- Series scrolled.
- Book Dates locked.
- Book Dates in edit mode.

That produces 43 native-resolution BMP files in one run. The public gallery publishes the representative X4 set once because X3 and X4 share the same stats interface, while both simulator targets remain part of release regression testing.

## Regenerate

Build the simulator, then run:

```bash
python3 scripts/run_simulator_smoke_test.py \
  --device x4 \
  --timeout 300 \
  --stats-screenshot-dir "$OUTPUT_DIR"
```

The script creates a new isolated temporary simulator filesystem on every run before seeding the gallery. Never point the simulator filesystem at a real SD card.

## Publication Gate

The alpha.7 gallery is generated directly from the release-candidate checkout. It remains review media until native-resolution inspection, OCR, metadata, and release-version checks pass.

After the source is synchronized:

1. Rebuild the simulator from the exact release candidate.
2. Compare the capture inventory with the implemented top-level and detail pages so every current stats screen is included.
3. Regenerate all 43 images, or the updated count if the implemented page inventory has changed.
4. Inspect every image at native resolution for clipping and stale labels.
5. Confirm values agree across Current, Device, Synced, Wrapped, and Library.
6. Run OCR and metadata checks.
7. Record the exact firmware version in the media manifest.
