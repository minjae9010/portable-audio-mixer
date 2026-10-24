#pragma once
#include "session_mixer.hpp"

namespace sessions {
struct AddedProgram { std::wstring key, name; std::shared_ptr<mix::OutputControl> control; };
class AddedPrograms {
    std::vector<AddedProgram> items_;
public:
    bool contains(const std::wstring& key) const {
        return std::any_of(items_.begin(),items_.end(),[&](const auto& p) { return p.key==key; });
    }
    bool add(std::wstring key,std::wstring name) {
        if(key.empty() || contains(key)) return false;
        items_.push_back({std::move(key),std::move(name),std::make_shared<mix::OutputControl>()}); return true;
    }
    void remove(size_t index) { if(index<items_.size()) items_.erase(items_.begin()+index); }
    std::vector<std::wstring> keys() const {
        std::vector<std::wstring> result;
        for(const auto& p:items_) result.push_back(p.key);
        return result;
    }
    std::vector<Channel> reconcile(const std::vector<Channel>& available) const {
        std::vector<Channel> result;
        for(const auto& p:items_) {
            Channel merged; merged.id=p.key; merged.name=p.name; merged.device=L"오디오 대기 중";
            merged.inputControl=p.control; merged.output=p.control.get();
            size_t sources=0;
            for(const auto& c:available) {
                if(applicationKey(c)!=p.key) continue;
                if(!sources++) { merged.pid=c.pid; merged.application=c.application; merged.device=c.device; }
                if(c.pid && std::find(merged.pids.begin(),merged.pids.end(),c.pid)==merged.pids.end()) merged.pids.push_back(c.pid);
                merged.volumes.insert(merged.volumes.end(),c.volumes.begin(),c.volumes.end());
                merged.meters.insert(merged.meters.end(),c.meters.begin(),c.meters.end());
                merged.playing=merged.playing || c.playing;
            }
            if(sources>1) merged.device=L"출력 세션 "+std::to_wstring(sources)+L"개";
            merged.read(); result.push_back(std::move(merged));
        }
        return result;
    }
};
} // namespace sessions
