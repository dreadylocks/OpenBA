| Type | Name | Desc |
|---|---|---|
| Fix | Status icons appear in place | New status icons no longer slide in from the map origin. |
| Fix | House "go to" button | Centers on the resident instead of rare case when it would not. |
| Fix | Tavern statistics | Counts the good actually consumed (edge case). |
| Fix | Grounded starting buildings | Lower starter barn to actually sit h terrain |
| Fix | Steady dialogs on UI rescale | Open dialogs no longer flash to a corner, instead stay centered. |
| Ext A | Eye-level camera | Follow a citizen at eye level (replaces overhead follow). |
| Ext A | Sharp UI | Scale text with UI so it remains sharp. Note this requires the release TTF files be installed |
| Ext A | Skip unseen work | Skip offscreen animations, particles to save cycles. |
| Ext A | Faster scene gathering | Skips animations/pathing of gathering offscreen objects. |
| Ext A | Batched walking | Batches walking operations above 1x speed. |
| Ext A | Render thread | Renders on a 2nd thread. |
| Ext A | Parallel culling | Take the ofscreeen work culling algo and spread across multiple threads. |
| Ext A | Quick loading screens | Loading and save screens close/open faster. |
| Ext A | Ambient occlusion | Screen-space AO. |
| Ext A | Better trees | Smoothes leaf edges, adds ability to adjust tree billboard shadows.|
| Ext A | Max shadow resolution | Adjustable shadow max resolution. |
| Ext A | Seamless ambient loops | When loops (ambient) end cleanly cycle into the next one. |
| Ext A | Background save compression | Compresses the save on a worker thread; identical save files. |
| Ext A | Background saving | Writes the save on a worker thread while the town holds still for a moment; the screen and camera keep moving. Identical save files. |
| Ext B | Faster pathfinding | Looks for same length routes to save pathfinding work. |
| Ext B | Parallel pathfinding | Spread pathfinding to multiple cores. |
| Ext B | Batched animation | Batch animations together above 1x speed. |
| Ext B | Batched walking, faster | Precache walking to save animation cycles. |
| Ext B | Spread warmth searches | At high speed, cold citizens pick a warm house a few frames later instead of all in one frame. |
| Ext B | Split the job reassignment | At high speed, the five-minute job assignment runs one frame after the housing pass. |
| Ext C | Neighbour towns | (this fork) Connects your town to a friend's through a relay server; their messages appear in the event log. `docs/fork/NEIGHBOUR_TOWNS.md`. |