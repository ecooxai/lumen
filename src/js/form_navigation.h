/* Included inside windows.cc's private namespace after its navigation helpers.
 * Forms share the window registry, immutable sandbox decisions and ownership
 * checks. Queue records retain one real form node and copied request bytes;
 * no borrowed JsCtx, DOM wrapper or page-provided target object is retained.
 */
bool form_context_active(JsCtx *c) {
    for (JsCtx *at = c; at; at = at->parent) {
        if (!current_context(at)) return false;
        if (at->parent && (!at->frame_el || !(at->frame_el->flags & NF_CONNECTED) ||
                          at->frame_el->doc != at->parent->doc)) return false;
    }
    return c != nullptr;
}
JsCtx *form_owner(Node *form) {
    if (!node_is(form, A_form) || !(form->flags & NF_CONNECTED) || !form->doc) return nullptr;
    JsCtx *source = owner_ctx(form->doc);
    return form_context_active(source) ? source : nullptr;
}
Node *form_submitter(Node *form, v8::Local<v8::Value> value) {
    Node *node = junwrap(value);
    return node && node->doc == form->doc &&
           (node_is(node, A_input) || node_is(node, A_button)) ? node : nullptr;
}
bool form_attribute(Node *node, const char *name, std::string &value) {
    size_t length = 0;
    const char *raw = node ? node_attr_n(node, name, &length) : nullptr;
    if (!raw) return false;
    /* Keep presence distinct from absence even for an over-limit attribute.
     * Callers reject the 65,537-byte sentinel length before using its prefix. */
    value.assign(raw, std::min(length, size_t(65537)));
    return true;
}
std::string form_target_name(Node *form, Node *submitter) {
    std::string name;
    bool present = form_attribute(submitter, "formtarget", name) ||
                   form_attribute(form, "target", name);
    if (!present) {
        for (Node *node = form->doc->node.first; node;
             node = node_next_in_tree(node, &form->doc->node)) {
            if (node_is(node, A_base) && form_attribute(node, "target", name)) break;
        }
    }
    /* HTML get-an-element's-target prevents dangling-markup target capture. */
    if (name.find('<') != std::string::npos && name.find_first_of("\t\n\r") != std::string::npos)
        name = "_blank";
    return name;
}
Window *form_target(JsCtx *source, const std::string &name, bool noopener) {
    Window *self = live_window(source->window_id);
    if (!self) return nullptr;
    std::string reserved = name;
    for (char &ch : reserved) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    if (name.empty() || reserved == "_self") return self;
    if (reserved == "_parent") return live_window(self->parent ? self->parent : self->id);
    if (reserved == "_top") return live_window(top_id(self->id));
    if (noopener || reserved == "_blank") return nullptr;
    Window *found = nullptr;
    for (auto &entry : windows) {
        Window *candidate = entry.second.get();
        if (!candidate->closing && candidate->group == self->group && candidate->name == name &&
            form_context_active(candidate->current) && (!found || candidate->id < found->id))
            found = candidate;
    }
    return found;
}
bool form_body(v8::Isolate *iso, v8::Local<v8::Value> value, std::string &body) {
    if (value->IsString()) {
        auto string = value.As<v8::String>();
        if ((size_t)string->Utf8Length(iso) > form_max_bytes) return false;
        body = jcstr(iso, value);
        return body.size() <= form_max_bytes;
    }
    const char *data = nullptr;
    size_t size = 0;
    if (value->IsArrayBuffer()) {
        auto buffer = value.As<v8::ArrayBuffer>();
        data = static_cast<const char *>(buffer->Data()); size = buffer->ByteLength();
    } else if (value->IsArrayBufferView()) {
        auto view = value.As<v8::ArrayBufferView>();
        auto buffer = view->Buffer();
        const size_t offset = view->ByteOffset();
        size = view->ByteLength();
        if (offset > buffer->ByteLength() || size > buffer->ByteLength() - offset) return false;
        if (size) data = static_cast<const char *>(buffer->Data()) + offset;
    } else return false;
    if (size > form_max_bytes || (size && !data)) return false;
    if (size) body.assign(data, size); else body.clear();
    return true;
}
void n_formCanNavigate(const FCI &args) {
    JsCtx *source = form_owner(junwrap(args[0]));
    args.GetReturnValue().Set(source && !(source->sandbox_flags & SB_FORMS));
}
void n_formAction(const FCI &args) {
    v8::Isolate *iso = args.GetIsolate();
    args.GetReturnValue().SetNull();
    Node *form = junwrap(args[0]);
    JsCtx *source = form_owner(form);
    if (!source || (source->sandbox_flags & SB_FORMS)) return;
    Node *submitter = form_submitter(form, args[1]);
    std::string input;
    if (!form_attribute(submitter, "formaction", input)) form_attribute(form, "action", input);
    if (input.empty()) input = source->doc->url ? source->doc->url : "about:blank";
    if (input.size() > 65536) return;
    const std::string base = js_windows_base_url(source, true);
    char *absolute = url_join_n(base.c_str(), input.data(), input.size());
    if (absolute && strlen(absolute) <= 65536) args.GetReturnValue().Set(jstr(iso, absolute));
    free(absolute);
}
void n_formNavigate(const FCI &args) {
    v8::Isolate *iso = args.GetIsolate();
    args.GetReturnValue().Set(false);
    Node *form = junwrap(args[0]);
    JsCtx *source = form_owner(form);
    if (!source || (source->sandbox_flags & SB_FORMS) || !args[2]->IsString()) return;
    std::string url;
    if (!window_string(iso, args[2], url)) return;
    URL parsed{};
    if (!url_parse_n(url.data(), url.size(), &parsed)) return;
    const bool post = !args[3]->IsNullOrUndefined();
    const bool script = !strcmp(parsed.scheme, "javascript");
    const bool data = !strcmp(parsed.scheme, "data");
    const bool supported = !strcmp(parsed.scheme, "http") || !strcmp(parsed.scheme, "https") ||
        (!post && (script || data || !strcmp(url.c_str(), "about:blank") || !strncmp(url.c_str(), "about:blank#", 12)));
    char *canonical = supported ? url_to_string(&parsed) : nullptr;
    url_free(&parsed);
    if (!canonical) return;
    url = canonical; free(canonical);
    if (url.size() > 65536) return;

    /* A just-inserted iframe must be a real initialized target before lookup.
     * Recheck source identity after frame initialization can invoke script. */
    frames_scan(source);
    if (form_owner(form) != source || (source->sandbox_flags & SB_FORMS)) return;
    Node *submitter = form_submitter(form, args[1]);
    const std::string name = form_target_name(form, submitter);
    if (name.size() > 65536) return;
    size_t rel_length = 0;
    const char *rel = node_attr_n(form, "rel", &rel_length);
    const bool noreferrer = token_n(rel, rel_length, "noreferrer");
    const bool noopener = noreferrer || token_n(rel, rel_length, "noopener");
    Window *target = form_target(source, name, noopener);
    if (!target) {
        /* Creating a new auxiliary with a POST body needs separate host support.
         * An unsupported new target must never silently submit the parent. */
        fprintf(stderr, "lumen: form submission has no supported existing target\n");
        return;
    }
    // Data GET submissions use the existing child-frame loader, which assigns
    // a fresh opaque origin and enforces frame-src, form-action and sandbox.
    // The top-level host has no equivalent data-navigation policy contract.
    if (data && !target->parent) return;
    if (!form_context_active(target->current) || !can_navigate(source, target)) return;
    /* The legacy top-level host accepts only URL/body/type, so it cannot carry
     * a different initiating document's origin/referrer/CSP snapshot. Keep
     * its existing top-self behavior; cross-document top targets require a
     * separate explicit host contract and must not inherit target metadata. */
    if (!target->parent && target->current != source) {
        fprintf(stderr, "lumen: cross-document top-level form submission is not supported\n");
        return;
    }
    if (target->parent && target->current->iso != source->iso) return;
    if (post && !target->parent && !target->current->host.navigate_post) return;
    if (!post && !script && !target->parent && !target->host.navigate && !target->current->host.navigate) return;

    FormNavigation request;
    request.source = source->window_id; request.source_epoch = source->window_epoch;
    request.target = target->id; request.target_epoch = target->epoch;
    request.url = std::move(url); request.submission.post = post;
    auto &submission = request.submission;
    if (post) {
        if (!form_body(iso, args[3], submission.body) || !args[4]->IsString() ||
            !window_string(iso, args[4], submission.content_type)) return;
        if (submission.content_type.find_first_of("\r\n") != std::string::npos ||
            submission.content_type.find('\0') != std::string::npos) return;
    }
    submission.origin = source->origin;
    submission.top_url = js_top_url(source) ? js_top_url(source) : "";
    submission.referrer = noreferrer ? "" : navigation_referrer(source, request.url);
    submission.csp = source->doc->csp ? source->doc->csp : "";
    submission.permissions_policy = source->doc->permissions_policy ? source->doc->permissions_policy : "";
    submission.policy_url = source->doc->csp_self_url ? source->doc->csp_self_url :
        blank_url(source->doc->url) && !source->inherited_origin_url.empty() ?
        source->inherited_origin_url : source->doc->url ? source->doc->url : "";
    if (!csp_allows_url(submission.csp.c_str(), submission.policy_url.c_str(),
                        request.url.c_str(), "form-action")) return;
    request.bytes = sizeof(FormNavigation) + request.url.size() + submission.body.size() +
        submission.content_type.size() + submission.origin.size() + submission.top_url.size() +
        submission.referrer.size() + submission.csp.size() + submission.policy_url.size() + submission.permissions_policy.size();
    auto old = std::find_if(form_navigations.begin(), form_navigations.end(),
        [&](const FormNavigation &item) { return item.form.get() == form; });
    const size_t previous = old == form_navigations.end() ? 0 : old->bytes;
    if (request.bytes > form_max_bytes || form_pending_bytes - previous > form_max_bytes - request.bytes ||
        (old == form_navigations.end() && form_navigations.size() >= form_max_count)) return;
    if (old != form_navigations.end()) {
        release_form_accounting(*old);
        form_navigations.erase(old);
    }
    node_retain(form); request.form.reset(form);
    request.sequence = next_form_navigation++;
    form_pending_bytes += request.bytes;
    ++live_window(request.source)->form_source_count;
    ++target->form_target_count;
    form_navigations.push_back(std::move(request));
    if (js_wakeup) js_wakeup();
    args.GetReturnValue().Set(true);
}
void deliver_form_navigations(JsCtx *source) {
    if (!form_context_active(source)) return;
    const uint64_t cutoff = next_form_navigation - 1;
    for (unsigned delivered = 0; delivered < 64; ++delivered) {
        auto next = std::find_if(form_navigations.begin(), form_navigations.end(),
            [&](const FormNavigation &item) { return item.source == source->window_id && item.sequence <= cutoff; });
        if (next == form_navigations.end()) break;
        FormNavigation request = std::move(*next);
        form_navigations.erase(next);
        release_form_accounting(request);
        Window *target = live_window(request.target);
        if (!form_context_active(source) || source->window_epoch != request.source_epoch ||
            (source->sandbox_flags & SB_FORMS) || !request.form ||
            request.form->doc != source->doc || !target || target->epoch != request.target_epoch ||
            !form_context_active(target->current) || !can_navigate(source, target)) continue;
        if (!target->parent && target->current != source) continue;
        /* The retained form has served its identity purpose. Release before
         * any navigation can retire its owner; the request now owns only bytes. */
        request.form.reset();
        if (!strncmp(request.url.c_str(), "javascript:", 11)) {
            js_windows_navigate(source, target->current, request.url, false);
        } else if (target->parent) {
            if (target->current->iso == source->iso)
                frame_submit(target->current, request.url, request.submission);
        } else if (request.submission.post) {
            /* Existing top-self host path. Captured form-action enforcement
             * through redirects belongs to frame_submit's NetRequest; this
             * legacy top host has no request-policy parameter. */
            const JsHost host = target->current->host;
            if (host.navigate_post)
                host.navigate_post(host.ud, request.url.c_str(), request.submission.body.data(),
                                   request.submission.body.size(), request.submission.content_type.c_str());
        } else if (target->host.navigate) {
            JsWindowNavigate navigation{target->id, request.url.c_str(),
                request.submission.referrer.empty() ? nullptr : request.submission.referrer.c_str(), false};
            target->host.navigate(target->host.ud, &navigation);
        } else {
            const JsHost host = target->current->host;
            if (host.navigate) host.navigate(host.ud, request.url.c_str());
        }
        if (!form_context_active(source)) break;
    }
}
