#pragma once

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <utility>

#include <QThread>

#include "tools/replay/camera.h"
#include "tools/replay/route.h"
#include "tools/replay/timeline.h"

#define DEMO_ROUTE "a2a0ccea32023010|2023-07-27--13-01-19"

// one segment uses about 100M of memory
constexpr int MIN_SEGMENTS_CACHE = 5;

enum REPLAY_FLAGS {
  REPLAY_FLAG_NONE = 0x0000,
  REPLAY_FLAG_DCAM = 0x0002,
  REPLAY_FLAG_ECAM = 0x0004,
  REPLAY_FLAG_NO_LOOP = 0x0010,
  REPLAY_FLAG_NO_FILE_CACHE = 0x0020,
  REPLAY_FLAG_QCAMERA = 0x0040,
  REPLAY_FLAG_NO_HW_DECODER = 0x0100,
  REPLAY_FLAG_NO_VIPC = 0x0400,
  REPLAY_FLAG_ALL_SERVICES = 0x0800,
};

typedef bool (*replayEventFilter)(const Event *, void *);
typedef std::map<int, std::unique_ptr<Segment>> SegmentMap;

class Replay : public QObject {
  Q_OBJECT

public:
  Replay(const std::string &route, std::vector<std::string> allow, std::vector<std::string> block, SubMaster *sm = nullptr,
         uint32_t flags = REPLAY_FLAG_NONE, const std::string &data_dir = "", QObject *parent = 0);
  ~Replay();
  bool load();
  RouteLoadError lastRouteError() const { return route_->lastError(); }
  void start(int seconds = 0);
  void stop();
  void pause(bool pause);
  void seekToFlag(FindFlag flag);
  void seekTo(double seconds, bool relative);
  inline bool isPaused() const { return user_paused_; }
  // the filter is called in streaming thread.try to return quickly from it to avoid blocking streaming.
  // the filter function must return true if the event should be filtered.
  // otherwise it must return false.
  inline void installEventFilter(replayEventFilter filter, void *opaque) {
    filter_opaque = opaque;
    event_filter = filter;
  }
  inline int segmentCacheLimit() const { return segment_cache_limit; }
  inline void setSegmentCacheLimit(int n) { segment_cache_limit = std::max(MIN_SEGMENTS_CACHE, n); }
  inline bool hasFlag(REPLAY_FLAGS flag) const { return flags_ & flag; }
  void setLoop(bool loop) { loop ? flags_ &= ~REPLAY_FLAG_NO_LOOP : flags_ |= REPLAY_FLAG_NO_LOOP; }
  bool loop() const { return !(flags_ & REPLAY_FLAG_NO_LOOP); }
  inline const Route* route() const { return route_.get(); }
  inline double currentSeconds() const { return double(cur_mono_time_ - route_start_ts_) / 1e9; }
  inline std::time_t routeDateTime() const { return route_date_time_; }
  inline uint64_t routeStartNanos() const { return route_start_ts_; }
  inline double toSeconds(uint64_t mono_time) const { return (mono_time - route_start_ts_) / 1e9; }
  inline double minSeconds() const { return !segments_.empty() ? segments_.begin()->first * 60 : 0; }
  inline double maxSeconds() const { return max_seconds_; }
  inline void setSpeed(float speed) { speed_ = speed; }
  inline float getSpeed() const { return speed_; }
  inline const SegmentMap &segments() const { return segments_; }
  inline const std::string &carFingerprint() const { return car_fingerprint_; }
  inline const std::shared_ptr<std::vector<Timeline::Entry>> getTimeline() const { return timeline_.getEntries(); }
  inline const std::optional<Timeline::Entry> findAlertAtTime(double sec) const { return timeline_.findAlertAtTime(sec); }

  // Event callback functions
  std::function<void(std::shared_ptr<LogReader>)> onQLogLoaded = nullptr;


signals:
  void streamStarted();
  void segmentsMerged();
  void seeking(double sec);
  void seekedTo(double sec);
  void minMaxTimeChanged(double min_sec, double max_sec);

protected:
  std::optional<uint64_t> find(FindFlag flag);
  void pauseStreamThread();
  void startStream(const Segment *cur_segment);
  void streamThread();
  void updateSegmentsCache();
  void loadSegmentInRange(SegmentMap::iterator begin, SegmentMap::iterator cur, SegmentMap::iterator end);
  void segmentLoadFinished(int seg_num, bool success);
  void mergeSegments(const SegmentMap::iterator &begin, const SegmentMap::iterator &end);
  void updateEvents(const std::function<bool()>& update_events_function);
  std::vector<Event>::const_iterator publishEvents(std::vector<Event>::const_iterator first,
                                                   std::vector<Event>::const_iterator last);
  void publishMessage(const Event *e);
  void publishFrame(const Event *e);
  void checkSeekProgress();
  inline bool isSegmentMerged(int n) const { return merged_segments_.count(n) > 0; }

  Timeline timeline_;

  pthread_t stream_thread_id = 0;
  QThread *stream_thread_ = nullptr;
  std::mutex stream_lock_;
  bool user_paused_ = false;
  std::condition_variable stream_cv_;
  std::atomic<int> current_segment_ = 0;
  std::optional<double> seeking_to_;
  SegmentMap segments_;
  // the following variables must be protected with stream_lock_
  std::atomic<bool> exit_ = false;
  std::atomic<bool> paused_ = false;
  bool events_ready_ = false;
  std::time_t route_date_time_;
  uint64_t route_start_ts_ = 0;
  std::atomic<uint64_t> cur_mono_time_ = 0;
  std::atomic<double> max_seconds_ = 0;
  std::vector<Event> events_;
  std::set<int> merged_segments_;

  // messaging
  SubMaster *sm = nullptr;
  std::unique_ptr<PubMaster> pm;
  std::vector<const char*> sockets_;
  std::vector<bool> filters_;
  std::unique_ptr<Route> route_;
  std::unique_ptr<CameraServer> camera_server_;
  std::atomic<uint32_t> flags_ = REPLAY_FLAG_NONE;

  std::string car_fingerprint_;
  std::atomic<float> speed_ = 1.0;
  replayEventFilter event_filter = nullptr;
  void *filter_opaque = nullptr;
  int segment_cache_limit = MIN_SEGMENTS_CACHE;
};
