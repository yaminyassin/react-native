/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "EventEmitterWrapper.h"
#include <fbjni/fbjni.h>
#include <react/renderer/components/view/PointerEvent.h>
#include <react/timing/primitives.h>

#include <utility>

using namespace facebook::jni;

namespace facebook::react {

namespace {

/*
 * Converts a Java timestamp (milliseconds since boot from
 * SystemClock.uptimeMillis()) to a HighResTimeStamp.
 */
HighResTimeStamp highResTimeStampFromMillis(jlong millis) {
  return HighResTimeStamp::fromChronoSteadyClockTimePoint(
      std::chrono::steady_clock::time_point(std::chrono::milliseconds(millis)));
}

bool isPointerEventName(const std::string& eventName) {
  return eventName == "topPointerDown" || eventName == "topPointerMove" ||
      eventName == "topPointerUp" || eventName == "topPointerCancel" ||
      eventName == "topPointerEnter" || eventName == "topPointerLeave" ||
      eventName == "topPointerOver" || eventName == "topPointerOut" ||
      eventName == "topClick";
}

bool isPointerEventPayload(const folly::dynamic& payload) {
  return payload.isObject() && payload.count("pointerId") != 0 &&
      payload.count("pointerType") != 0 && payload.count("clientX") != 0;
}

/*
 * Builds a typed PointerEvent from the WritableMap payload produced by
 * Android's PointerEvent.createW3CPointerEvent, so that pointer events reach
 * PointerEventsProcessor via UIManagerBinding instead of bypassing it as a
 * dynamic payload. timeStamp is intentionally not read from the map's
 * "timestamp" key; it comes from the JNI event timestamp parameter.
 */
PointerEvent pointerEventFromPayload(const folly::dynamic& payload) {
  // Only three keys are guaranteed by the caller's guard; a custom-emitted
  // event may reuse a pointer event name with a partial payload, so every
  // read falls back to the struct's zero-init default instead of throwing.
  PointerEvent event{};
  event.pointerId =
      static_cast<int>(payload.getDefault("pointerId", 0.0).asDouble());
  event.pressure =
      static_cast<Float>(payload.getDefault("pressure", 0.0).asDouble());
  event.pointerType = payload.getDefault("pointerType", "").getString();
  event.clientPoint = {
      .x = static_cast<Float>(payload.getDefault("clientX", 0.0).asDouble()),
      .y = static_cast<Float>(payload.getDefault("clientY", 0.0).asDouble())};
  event.screenPoint = {
      .x = static_cast<Float>(payload.getDefault("screenX", 0.0).asDouble()),
      .y = static_cast<Float>(payload.getDefault("screenY", 0.0).asDouble())};
  event.offsetPoint = {
      .x = static_cast<Float>(payload.getDefault("offsetX", 0.0).asDouble()),
      .y = static_cast<Float>(payload.getDefault("offsetY", 0.0).asDouble())};
  event.width =
      static_cast<Float>(payload.getDefault("width", 0.0).asDouble());
  event.height =
      static_cast<Float>(payload.getDefault("height", 0.0).asDouble());
  event.tiltX = static_cast<int>(payload.getDefault("tiltX", 0.0).asDouble());
  event.tiltY = static_cast<int>(payload.getDefault("tiltY", 0.0).asDouble());
  event.detail = static_cast<int>(payload.getDefault("detail", 0).asInt());
  event.buttons = static_cast<int>(payload.getDefault("buttons", 0).asInt());
  event.tangentialPressure = static_cast<Float>(
      payload.getDefault("tangentialPressure", 0.0).asDouble());
  event.twist = static_cast<int>(payload.getDefault("twist", 0).asInt());
  event.ctrlKey = payload.getDefault("ctrlKey", false).asBool();
  event.shiftKey = payload.getDefault("shiftKey", false).asBool();
  event.altKey = payload.getDefault("altKey", false).asBool();
  event.metaKey = payload.getDefault("metaKey", false).asBool();
  event.isPrimary = payload.getDefault("isPrimary", false).asBool();
  event.button = static_cast<int>(payload.getDefault("button", 0).asInt());
  return event;
}

} // namespace

void EventEmitterWrapper::dispatchEvent(
    std::string eventName,
    NativeMap* payload,
    int category,
    jlong eventTimestamp) {
  // It is marginal, but possible for this to be constructed without a valid
  // EventEmitter. In those cases, make sure we noop/blackhole events instead of
  // crashing.
  if (eventEmitter == nullptr) {
    return;
  }

  auto timeStamp = highResTimeStampFromMillis(eventTimestamp);
  // NativeMap::consume can only be called once, so materialize the payload
  // before deciding which dispatch path to take.
  folly::dynamic payloadDynamic =
      (payload != nullptr) ? payload->consume() : folly::dynamic::object();

  if (isPointerEventName(eventName) && isPointerEventPayload(payloadDynamic)) {
    auto pointerEvent = pointerEventFromPayload(payloadDynamic);
    pointerEvent.timeStamp = timeStamp;
    eventEmitter->dispatchEvent(
        std::move(eventName),
        std::make_shared<PointerEvent>(std::move(pointerEvent)),
        static_cast<RawEvent::Category>(category),
        timeStamp);
  } else {
    eventEmitter->dispatchEvent(
        std::move(eventName),
        std::move(payloadDynamic),
        static_cast<RawEvent::Category>(category),
        timeStamp);
  }
}

void EventEmitterWrapper::dispatchEventSynchronously(
    std::string eventName,
    NativeMap* params,
    jlong eventTimestamp) {
  // It is marginal, but possible for this to be constructed without a valid
  // EventEmitter. In those cases, make sure we noop/blackhole events instead of
  // crashing.
  if (eventEmitter != nullptr) {
    eventEmitter->experimental_flushSync([&]() {
      eventEmitter->dispatchEvent(
          std::move(eventName),
          (params != nullptr) ? params->consume() : folly::dynamic::object(),
          RawEvent::Category::Discrete,
          highResTimeStampFromMillis(eventTimestamp));
    });
  }
}

void EventEmitterWrapper::dispatchUniqueEvent(
    std::string eventName,
    NativeMap* payload,
    jlong eventTimestamp) {
  // It is marginal, but possible for this to be constructed without a valid
  // EventEmitter. In those cases, make sure we noop/blackhole events instead of
  // crashing.
  if (eventEmitter == nullptr) {
    return;
  }

  auto timeStamp = highResTimeStampFromMillis(eventTimestamp);
  // NativeMap::consume can only be called once, so materialize the payload
  // before deciding which dispatch path to take.
  folly::dynamic payloadDynamic =
      (payload != nullptr) ? payload->consume() : folly::dynamic::object();

  if (isPointerEventName(eventName) && isPointerEventPayload(payloadDynamic)) {
    auto pointerEvent = pointerEventFromPayload(payloadDynamic);
    pointerEvent.timeStamp = timeStamp;
    eventEmitter->dispatchUniqueEvent(
        std::move(eventName),
        std::make_shared<PointerEvent>(std::move(pointerEvent)),
        timeStamp);
  } else {
    eventEmitter->dispatchUniqueEvent(
        std::move(eventName), std::move(payloadDynamic), timeStamp);
  }
}

void EventEmitterWrapper::registerNatives() {
  registerHybrid({
      makeNativeMethod("dispatchEvent", EventEmitterWrapper::dispatchEvent),
      makeNativeMethod(
          "dispatchUniqueEvent", EventEmitterWrapper::dispatchUniqueEvent),
      makeNativeMethod(
          "dispatchEventSynchronously",
          EventEmitterWrapper::dispatchEventSynchronously),
  });
}

} // namespace facebook::react
