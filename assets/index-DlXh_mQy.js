(function(){let e=document.createElement(`link`).relList;if(e&&e.supports&&e.supports(`modulepreload`))return;for(let e of document.querySelectorAll(`link[rel="modulepreload"]`))n(e);new MutationObserver(e=>{for(let t of e)if(t.type===`childList`)for(let e of t.addedNodes)e.tagName===`LINK`&&e.rel===`modulepreload`&&n(e)}).observe(document,{childList:!0,subtree:!0});function t(e){let t={};return e.integrity&&(t.integrity=e.integrity),e.referrerPolicy&&(t.referrerPolicy=e.referrerPolicy),t.credentials=e.crossOrigin===`use-credentials`?`include`:e.crossOrigin===`anonymous`?`omit`:`same-origin`,t}function n(e){if(e.ep)return;e.ep=!0;let n=t(e);fetch(e.href,n)}})();var e=`// Host audition only. The WASM kernel owns sound; this runner owns eight voices,
// pitch buffers, a simple audition gate and mixing. No stock FX emulation.
function bounded(value, low, high, name) {
  if (!Number.isInteger(value) || value < low || value > high) throw new Error(\`Invalid \${name}\`);
  return value;
}

class PreviewEngine {
  constructor(module, sampleRate, parameters) {
    if (!(module instanceof WebAssembly.Module)) throw new Error('A compiled preview module is required');
    if (WebAssembly.Module.imports(module).length) throw new Error('Preview modules must be self-contained');
    this.api = new WebAssembly.Instance(module).exports;
    const api = this.api;
    if (api.opxy_preview_abi() !== 1) throw new Error('Unsupported preview ABI');
    this.sampleRate = bounded(api.opxy_sample_rate(), 8000, 192000, 'kernel sample rate');
    if (sampleRate !== this.sampleRate) throw new Error(\`Kernel requires \${this.sampleRate} Hz\`);
    this.blockFrames = bounded(api.opxy_block_frames(), 1, 256, 'block length');
    this.stateBytes = bounded(api.opxy_state_bytes(), 4, api.memory.buffer.byteLength, 'voice state size');
    this.parameterCount = bounded(api.opxy_parameter_count(), 1, 8, 'parameter count');
    const alignment = bounded(api.opxy_state_alignment(), 4, 64, 'voice alignment');
    if (alignment & (alignment - 1)) throw new Error('Voice alignment must be a power of two');
    const voiceCount = bounded(api.opxy_voice_capacity(), 1, 8, 'audition voice count');
    const peak = bounded(api.opxy_output_peak_q31(), 1, 0x7fffffff, 'kernel headroom');
    this.auditionGain = 0.1 / peak;
    const memory = api.memory.buffer;
    let next = bounded(Number(api.__heap_base.value), 0, memory.byteLength, 'heap base');
    const allocate = (bytes, align = 4) => {
      const address = Math.ceil(next / align) * align;
      next = address + bytes;
      if (next > memory.byteLength) throw new Error('Preview memory budget exceeded');
      return address;
    };
    this.parameterAddress = allocate(this.parameterCount * 4);
    this.parameters = new Int32Array(memory, this.parameterAddress, this.parameterCount);
    this.voices = Array.from({ length: voiceCount }, (_, index) => {
      const state = bounded(api.opxy_voice_state(index), 1, Number(api.__heap_base.value) - this.stateBytes,
                            'voice address');
      if (state % alignment) throw new Error('Misaligned voice state');
      const pitchAddress = allocate(this.blockFrames * 4);
      const outputAddress = allocate(this.blockFrames * 4);
      api.opxy_initialize(state);
      return { state, pitchAddress, outputAddress,
        pitch: new Uint32Array(memory, pitchAddress, this.blockFrames),
        output: new Int32Array(memory, outputAddress, this.blockFrames),
        active: false, note: 0, age: 0, velocity: 0, amplitude: 0, releasing: false,
        cursor: this.blockFrames };
    });
    for (let i = 0; i < voiceCount; ++i) for (let j = 0; j < i; ++j) {
      if (Math.abs(this.voices[i].state - this.voices[j].state) < this.stateBytes) {
        throw new Error('Overlapping voice state');
      }
    }
    this.clock = 0;
    this.attackStep = 1 / (sampleRate * 0.005);
    this.releaseStep = 1 / (sampleRate * 0.12);
    this.setParameters(parameters);
  }

  setParameters(values) {
    if (!Array.isArray(values) || values.length !== this.parameterCount ||
        values.some(v => !Number.isInteger(v) || v < 0 || v > 0x7fffffff)) {
      throw new Error('Expected the defined nonnegative Q31 parameter words');
    }
    this.parameters.set(values);
    // A partially consumed kernel block finishes before new controls apply.
    // At 44.1 kHz the bound is 63 samples (1.43 ms); never restart voice phase.
  }

  // A drum kit's previewed model; synth previews ignore it.
  setModel(model) {
    if (!Number.isInteger(model) || model < 0 || model > 127) throw new Error('Invalid drum model');
    if (this.api.opxy_preview_model) this.api.opxy_preview_model(model);
  }
  noteOn(note, velocity = 0.8) {
    if (!Number.isInteger(note) || note < 0 || note > 127 ||
        !Number.isFinite(velocity) || velocity <= 0 || velocity > 1) return false;
    let voice = this.voices.find(v => v.active && v.note === note) || this.voices.find(v => !v.active);
    if (!voice) voice = this.voices.reduce((a, b) => a.age < b.age ? a : b);
    if (!voice.active || voice.note !== note) {
      this.api.opxy_destroy(voice.state);
      this.api.opxy_initialize(voice.state);
    }
    this.api.opxy_start(voice.state, note);
    voice.pitch.fill(Math.round(440 * 2 ** ((note - 69) / 12) / this.sampleRate * 0x100000000));
    voice.active = true; voice.note = note; voice.age = ++this.clock;
    voice.velocity = velocity; voice.amplitude = 0; voice.releasing = false;
    voice.cursor = this.blockFrames;
    return true;
  }
  noteOff(note) {
    for (const voice of this.voices) if (voice.active && voice.note === note) voice.releasing = true;
  }
  panic() { for (const voice of this.voices) {
    if (voice.active) this.api.opxy_destroy(voice.state);
    voice.active = false;
  } }

  render(output) {
    // No typed-array creation, memory growth, transcendental functions or messages
    // here. Each voice retains its partial block across arbitrary host quanta.
    for (let i = 0; i < output.length; ++i) {
      let sample = 0;
      for (let j = 0; j < this.voices.length; ++j) {
        const voice = this.voices[j];
        if (!voice.active) continue;
        voice.amplitude = voice.releasing ? Math.max(0, voice.amplitude - this.releaseStep)
                                          : Math.min(1, voice.amplitude + this.attackStep);
        if (voice.releasing && voice.amplitude === 0) {
          this.api.opxy_destroy(voice.state);
          voice.active = false; continue;
        }
        if (voice.cursor === this.blockFrames) {
          this.api.opxy_render(voice.state, voice.outputAddress, this.parameterAddress, voice.pitchAddress);
          voice.cursor = 0;
        }
        // Compiled headroom determines audition normalization, peak 0.1/voice.
        sample += voice.output[voice.cursor++] * this.auditionGain * voice.amplitude * voice.velocity;
      }
      output[i] = sample;
    }
  }
}



class PreviewProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const { module, parameters } = options.processorOptions;
    this.engine = new PreviewEngine(module, sampleRate, parameters);
    this.port.onmessage = ({ data }) => {
      try {
        if (data.type === 'parameters') this.engine.setParameters(data.value);
        if (data.type === 'model') this.engine.setModel(data.value);
        if (data.type === 'on') this.engine.noteOn(data.note, data.velocity);
        if (data.type === 'off') this.engine.noteOff(data.note);
        if (data.type === 'panic') this.engine.panic();
      } catch (error) { this.port.postMessage({ error: error.message }); }
    };
    this.port.postMessage({ ready: true, backend: 'cpp-wasm', sampleRate,
      blockFrames: this.engine.blockFrames, stateBytes: this.engine.stateBytes });
  }
  process(_inputs, outputs) {
    const channels = outputs[0];
    if (channels?.length) {
      this.engine.render(channels[0]);
      for (let i = 1; i < channels.length; ++i) channels[i].set(channels[0]);
    }
    return true;
  }
}

registerProcessor('opxy-audition', PreviewProcessor);`,t=class{constructor(e,t,n){this.module=e,this.parameters=[...t],this.onState=n,this.volume=.5,this.revision=0,this.disposed=!1,this.playing=!1,this.held=null}async start(){let t=this.context=new AudioContext({sampleRate:44100,latencyHint:`interactive`});if(t.sampleRate!==44100)throw Error(`This browser cannot provide 44.1 kHz audio.`);let n=location.protocol===`file:`,r=n?`data:text/javascript;charset=utf-8,`+encodeURIComponent(e):URL.createObjectURL(new Blob([e],{type:`text/javascript`}));try{await t.audioWorklet.addModule(r)}finally{n||URL.revokeObjectURL(r)}if(this.disposed)return;let i=this.processor=new AudioWorkletNode(t,`opxy-audition`,{numberOfInputs:0,numberOfOutputs:1,outputChannelCount:[1],processorOptions:{module:this.module,parameters:this.parameters}}),a=this.gain=t.createGain();a.gain.value=0,i.connect(a).connect(t.destination),await new Promise((e,t)=>{let n=setTimeout(()=>t(Error(`Audio preview did not start.`)),5e3),r=e=>{clearTimeout(n),this.stop(),t(Error(e)),this.onState(!1,e)};i.onprocessorerror=()=>r(`Audio preview stopped. Select the machine again to retry.`),i.port.onmessage=({data:t})=>{t.ready&&(clearTimeout(n),e()),t.error&&r(t.error)}}),t.onstatechange=()=>{t.state!==`running`&&this.playing&&this.stop()}}async play(e){if(this.disposed)return;let t=++this.revision;this.held=e;try{this.starting??=this.start();let n=this.context?.resume()??Promise.resolve();if(await Promise.all([this.starting,n]),this.disposed||t!==this.revision)return;this.processor.port.postMessage({type:`panic`}),this.processor.port.postMessage({type:`parameters`,value:this.parameters}),this.gain.gain.value=this.volume,this.processor.port.postMessage({type:`on`,note:e,velocity:.8}),this.playing=!0,this.onState(!0),this.held!==e&&this.processor.port.postMessage({type:`off`,note:e})}catch(e){if(this.disposed||t!==this.revision)return;this.stop(),this.processor?.disconnect(),this.processor?.port.close(),await this.context?.close().catch(()=>{}),this.context=this.processor=this.gain=this.starting=null,this.onState(!1,e.message)}}release(e){this.held===e&&(this.held=null),this.playing&&this.processor?.port.postMessage({type:`off`,note:e})}stop(){this.revision++,this.playing=!1,this.gain&&this.context.state!==`closed`&&(this.gain.gain.value=0),this.processor?.port.postMessage({type:`panic`}),this.onState(!1)}setParameters(e){this.parameters=[...e],this.processor?.port.postMessage({type:`parameters`,value:this.parameters})}setVolume(e){this.volume=e,this.gain&&this.playing&&(this.gain.gain.value=e)}destroy(){this.disposed=!0,this.stop(),this.processor?.disconnect(),this.processor?.port.close(),this.context?.close().catch(()=>{})}},n=`modulepreload`,r=function(e,t){return new URL(e,t).href},i={},a=function(e){return e.pathname.endsWith(`.css`)},o=function(e,t,n){if(t in e)return e[t];let r=n();if(!r){e[t]=void 0;return}let i=r.then(()=>{e[t]=void 0},n=>{throw e[t]=void 0,n});return e[t]=i,i},s=function(e,t,s){let c=Promise.resolve();if(t&&t.length>0){let e,l=document.querySelector(`meta[property=csp-nonce]`),u=l?.nonce||l?.getAttribute(`nonce`);function d(e){return Promise.all(e.map(e=>Promise.resolve(e).then(e=>({status:`fulfilled`,value:e}),e=>({status:`rejected`,reason:e}))))}function f(e){return import.meta.resolve?new URL(import.meta.resolve(e)):new URL(e,import.meta.url)}c=d(t.map(t=>{t=r(t,s);let c=f(t),l=a(c);return o(i,c.href,()=>{if(e===void 0){e={all:new Set,styles:new Set};let t=document.getElementsByTagName(`link`);for(let n=t.length-1;n>=0;n--){let r=t[n];e.all.add(r.href),r.rel===`stylesheet`&&e.styles.add(r.href)}}if((l?e.styles:e.all).has(c.href))return;let t=document.createElement(`link`);if(t.rel=l?`stylesheet`:n,l||(t.as=`script`),t.crossOrigin=``,t.href=c.href,u&&t.setAttribute(`nonce`,u),document.head.appendChild(t),l)return new Promise((e,n)=>{t.addEventListener(`load`,e),t.addEventListener(`error`,()=>n(Error(`Unable to preload CSS for ${c}`)))})})}).filter(e=>e!==void 0))}function l(e){let t=new Event(`vite:preloadError`,{cancelable:!0});if(t.payload=e,window.dispatchEvent(t),!t.defaultPrevented)throw e}return c.then(t=>{for(let e of t||[])e.status===`rejected`&&l(e.reason);return e().catch(l)})};try{let e=await fetch(`./kit.json`,{cache:`no-store`});if(!e.ok)throw Error(`Machine collection unavailable`);let t=await e.json();document.getElementById(`kit-data`).textContent=JSON.stringify(t)}catch(e){console.error(`open-xy: configure OPEN_XY_KIT with a verified export for a complete preview.`,e)}try{let[{createBuilder:e},{createCatalogView:n}]=await Promise.all([s(()=>import(`./app-Bcw9g-K7.js`),[],import.meta.url),s(()=>import(`./catalog-view-C9ktjnFK.js`),[],import.meta.url)]);await e({audition:t,catalogView:n,browseFirst:!0})}catch(e){console.error(`open-xy failed to load.`,e);let t=document.createElement(`div`);t.className=`load-error`,t.setAttribute(`role`,`alert`);let n=document.createElement(`p`);n.textContent=`The builder could not load. Reload the page to try again.`;let r=document.createElement(`button`);r.type=`button`,r.className=`secondary`,r.textContent=`Reload page`,r.onclick=()=>location.reload(),t.append(n,r),document.getElementById(`browser-gate`).append(t)}