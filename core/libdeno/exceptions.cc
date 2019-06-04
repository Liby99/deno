// Copyright 2018-2019 the Deno authors. All rights reserved. MIT license.
#include "exceptions.h"
#include <string>

namespace deno {

void ReplaceAll(std::string &str, const std::string &from, const std::string &to) {
  std::string::size_type pos = 0u;
  while ((pos = str.find(from, pos)) != std::string::npos) {
    str.replace(pos, from.length(), to);
    pos += to.length();
  }
}

std::string EscapeString(std::string str) {
  ReplaceAll(str, "\"", "\\\"");
  return str;
}

std::string EncodeMessageAsJSON(v8::Local<v8::Context> context,
                                v8::Local<v8::Message> message) {
  auto *isolate = context->GetIsolate();
  v8::EscapableHandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(context);

  v8::String::Utf8Value _exception_str(isolate, message->Get());
  auto exception_str = std::string(*_exception_str);

  v8::String::Utf8Value _script_resource_name(isolate, message->GetScriptResourceName());
  auto script_resource_name = std::string(*_script_resource_name);

  auto start_position = message->GetStartPosition();
  auto end_position = message->GetEndPosition();
  auto error_level = message->ErrorLevel();
  auto is_shared_cross_origin = message->IsSharedCrossOrigin();
  auto is_opaque = message->IsOpaque();

  auto maybe_source_line = message->GetSourceLine(context);
  std::stringstream source_line;
  if (!maybe_source_line.IsEmpty()) {
    v8::String::Utf8Value value(isolate, maybe_source_line.ToLocalChecked());
    source_line << "\"sourceLine\": \"" << EscapeString(std::string(*value)) << "\",";
  }

  auto maybe_line_number = message->GetLineNumber(context);
  std::stringstream line_number;
  if (maybe_line_number.IsJust()) {
    line_number << "\"lineNumber\": " << maybe_line_number.FromJust() << ",";
  }

  auto maybe_start_column = message->GetStartColumn(context);
  std::stringstream start_column;
  if (maybe_start_column.IsJust()) {
    start_column << "\"startColumn\": " << maybe_start_column.FromJust() << ",";
  }

  auto maybe_end_column = message->GetEndColumn(context);
  std::stringstream end_column;
  if (maybe_end_column.IsJust()) {
    end_column << "\"endColumn\": " << maybe_end_column.FromJust() << ",";
  }

  auto stack_trace = message->GetStackTrace();
  std::stringstream stack_trace_json;
  if (!stack_trace.IsEmpty()) {
    stack_trace_json << "[";
    uint32_t count = static_cast<uint32_t>(stack_trace->GetFrameCount());
    for (uint32_t i = 0; i < count; ++i) {
      auto frame = stack_trace->GetFrame(isolate, i);

      v8::String::Utf8Value _function_name(isolate, frame->GetFunctionName());
      auto function_name = std::string(*_function_name);

      auto line = frame->GetLineNumber();
      auto column = frame->GetColumn();
      auto is_eval = frame->IsEval();
      auto is_constructor = frame->IsConstructor();
      auto is_wasm = frame->IsWasm();

      auto maybe_script_name = frame->GetScriptNameOrSourceURL();
      auto temp = maybe_script_name.IsEmpty() ? v8_str("<unknown>") : maybe_script_name;
      v8::String::Utf8Value _script_name(isolate, temp);
      auto script_name = std::string(*_script_name);

      stack_trace_json << "{\
        \"line\": " << line << ",\
        \"column\": " << column << ",\
        \"functionName\": \"" << function_name << "\",\
        \"scriptName\": \"" << script_name << "\",\
        \"isEval\": " << (is_eval ? "true" : "false") << ",\
        \"isConstructor\": " << (is_constructor ? "true" : "false") << ",\
        \"isWasm\": " << (is_wasm ? "true" : "false") << "\
      }";

      if (i < count - 1) {
        stack_trace_json << ",";
      }
    }
    stack_trace_json << "]";
  } else {

    std::stringstream line;
    if (!maybe_line_number.IsJust()) {
      line << "\"line\": " << maybe_line_number.FromJust() << ",";
    }

    std::stringstream column;
    if (!maybe_start_column.IsJust()) {
      column << "\"column\": " << maybe_start_column.FromJust() << ",";
    }

    auto script_str = v8::JSON::Stringify(context, message->GetScriptResourceName()).ToLocalChecked();
    v8::String::Utf8Value _script_str(isolate, script_str);
    auto script_name = std::string(*_script_str);

    stack_trace_json << "[{" <<
      line.str() <<
      column.str() <<
      "\"scriptName\": \"" << script_name << "\"\
    }]";
  }

  std::stringstream result;
  result << "{\
    \"message\": \"" << exception_str << "\",\
    \"scriptResourceName\": \"" << script_resource_name << "\",\
    \"startPosition\": " << start_position << ",\
    \"endPosition\": " << end_position << ",\
    \"errorLevel\": " << error_level << ",\
    \"isSharedCrossOrigin\": " << (is_shared_cross_origin ? "true" : "false") << ",\
    \"isOpaque\": " << (is_opaque ? "true" : "false") << "," <<
    source_line.str() <<
    line_number.str() <<
    start_column.str() <<
    end_column.str() <<
    "\"frames\": " << stack_trace_json.str() <<
  "}";

  // std::cout << result.str() << std::endl;

  return result.str();
}

std::string EncodeExceptionAsJSON(v8::Local<v8::Context> context,
                                  v8::Local<v8::Value> exception) {
  auto *isolate = context->GetIsolate();
  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(context);

  auto message = v8::Exception::CreateMessage(isolate, exception);
  return EncodeMessageAsJSON(context, message);
}

void HandleException(v8::Local<v8::Context> context,
                     v8::Local<v8::Value> exception) {
  v8::Isolate *isolate = context->GetIsolate();

  // TerminateExecution was called
  if (isolate->IsExecutionTerminating()) {
    // cancel exception termination so that the exception can be created
    isolate->CancelTerminateExecution();

    // maybe make a new exception object
    if (exception->IsNullOrUndefined()) {
      exception = v8::Exception::Error(v8_str("execution terminated"));
    }

    // handle the exception as if it is a regular exception
    HandleException(context, exception);

    // re-enable exception termination
    isolate->TerminateExecution();
    return;
  }

  DenoIsolate *d = DenoIsolate::FromIsolate(isolate);
  std::string json_str = EncodeExceptionAsJSON(context, exception);
  CHECK_NOT_NULL(d);
  d->last_exception_ = json_str;
}

void HandleExceptionMessage(v8::Local<v8::Context> context,
                            v8::Local<v8::Message> message) {
  v8::Isolate *isolate = context->GetIsolate();

  // TerminateExecution was called
  if (isolate->IsExecutionTerminating()) {
    HandleException(context, v8::Undefined(isolate));
    return;
  }

  DenoIsolate *d = DenoIsolate::FromIsolate(isolate);
  std::string json_str = EncodeMessageAsJSON(context, message);
  CHECK_NOT_NULL(d);
  d->last_exception_ = json_str;
}
} // namespace deno
