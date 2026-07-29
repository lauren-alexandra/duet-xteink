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

Recognizable books may appear as interface display text and cover art in the broader gallery. Their progress, reading time, pace, sessions, dates, and every other statistic are invented. The reproducible demo uses only public-domain classics, and all remaining fixture book and reader names are fictional.

## Capture Inventory

The gallery captures all 33 top-level Reading Stats tabs:

1. Current
2. Progress
3. Book
4. Trends
5. Activity
6. 90 Days
7. Calendar
8. Heatmap
9. Profile
10. Goals
11. Sessions
12. Weekdays
13. Pace
14. Time of Day
15. Months
16. Year
17. Devices
18. Sessions Mix
19. Streaks
20. Start/Finish
21. Dates
22. Reader DNA
23. DNA Details
24. Signature
25. Signature Details
26. Fastest
27. Wrapped
28. Started
29. Library
30. Taste
31. Series
32. Device
33. Synced

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

That produces 43 native-resolution BMP files in one run.

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

The alpha.6 gallery is generated directly from the public-release checkout. It remains private review media until native-resolution inspection, OCR, metadata, and release-version checks pass.

After the source is synchronized:

1. Rebuild the simulator from the exact release candidate.
2. Compare the capture inventory with the implemented top-level and detail pages so every current stats screen is included.
3. Regenerate all 43 images, or the updated count if the implemented page inventory has changed.
4. Inspect every image at native resolution for clipping and stale labels.
5. Confirm values agree across Current, Device, Synced, Wrapped, and Library.
6. Run OCR and metadata checks.
7. Record the exact firmware version in the media manifest.
