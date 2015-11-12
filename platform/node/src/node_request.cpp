#include "node_request.hpp"
#include "node_file_source.hpp"
#include <mbgl/storage/response.hpp>

#include <cmath>
#include <iostream>

namespace mbgl {

Nan::Persistent<v8::Function> NodeRequest::constructor;

NAN_MODULE_INIT(NodeRequest::Init) {
    v8::Local<v8::FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(New);

    tpl->InstanceTemplate()->SetInternalFieldCount(1);
    tpl->SetClassName(Nan::New("Request").ToLocalChecked());

    constructor.Reset(tpl->GetFunction());
    Nan::Set(target, Nan::New("Request").ToLocalChecked(), tpl->GetFunction());
}

NAN_METHOD(NodeRequest::New) {
    auto req = new NodeRequest(*reinterpret_cast<FileSource::Callback*>(info[0].As<v8::External>()->Value()));
    req->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

v8::Handle<v8::Object> NodeRequest::Create(const Resource& resource, FileSource::Callback callback) {
    Nan::EscapableHandleScope scope;

    v8::Local<v8::Value> argv[] = {
        Nan::New<v8::External>(const_cast<FileSource::Callback*>(&callback))
    };
    auto instance = Nan::New(constructor)->NewInstance(1, argv);

    Nan::Set(instance, Nan::New("url").ToLocalChecked(), Nan::New(resource.url).ToLocalChecked());
    Nan::Set(instance, Nan::New("kind").ToLocalChecked(), Nan::New<v8::Integer>(int(resource.kind)));

    return scope.Escape(instance);
}

NAN_METHOD(NodeRequest::Respond) {
    using Error = Response::Error;
    Response response;

    if (info.Length() < 1) {
        response.error = std::make_unique<Error>(Error::Reason::NotFound);
    } else if (info[0]->BooleanValue()) {
        // Store the error string.
        const Nan::Utf8String message { info[0]->ToString() };
        response.error = std::make_unique<Error>(
            Error::Reason::Other, std::string{ *message, size_t(message.length()) });
    } else if (info.Length() < 2 || !info[1]->IsObject()) {
        return Nan::ThrowTypeError("Second argument must be a response object");
    } else {
        auto res = info[1]->ToObject();

        if (Nan::Has(res, Nan::New("modified").ToLocalChecked()).FromJust()) {
            const double modified = Nan::Get(res, Nan::New("modified").ToLocalChecked()).ToLocalChecked()->ToNumber()->Value();
            if (!std::isnan(modified)) {
                response.modified = modified / 1000; // JS timestamps are milliseconds
            }
        }

        if (Nan::Has(res, Nan::New("expires").ToLocalChecked()).FromJust()) {
            const double expires = Nan::Get(res, Nan::New("expires").ToLocalChecked()).ToLocalChecked()->ToNumber()->Value();
            if (!std::isnan(expires)) {
                response.expires = expires / 1000; // JS timestamps are milliseconds
            }
        }

        if (Nan::Has(res, Nan::New("etag").ToLocalChecked()).FromJust()) {
            auto etagHandle = Nan::Get(res, Nan::New("etag").ToLocalChecked()).ToLocalChecked();
            if (etagHandle->BooleanValue()) {
                const Nan::Utf8String etag { etagHandle->ToString() };
                response.etag = std::string { *etag, size_t(etag.length()) };
            }
        }

        if (Nan::Has(res, Nan::New("data").ToLocalChecked()).FromJust()) {
            auto dataHandle = Nan::Get(res, Nan::New("data").ToLocalChecked()).ToLocalChecked();
            if (node::Buffer::HasInstance(dataHandle)) {
                response.data = std::make_shared<std::string>(
                    node::Buffer::Data(dataHandle),
                    node::Buffer::Length(dataHandle)
                );
            } else {
                return Nan::ThrowTypeError("Response data must be a Buffer");
            }
        }
    }

    // Send the response object to the NodeFileSource object
    Nan::ObjectWrap::Unwrap<NodeRequest>(info.Data().As<v8::Object>())->callback(response);
    info.GetReturnValue().SetUndefined();
}

NodeRequest::NodeRequest(FileSource::Callback callback_)
    : callback(callback_) {}
}
