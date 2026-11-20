/* Manual hardware test using SDROxide 1.7.2's actual SpyServer client crate.
 * Requires the experimental FFT server image and no other connected client. */
use std::{thread,time::{Duration,Instant}};
use sdroxide_spyserver::SpyServerHandle;
use sdroxide_types::SpyServerConfig;
fn receive(h:&mut SpyServerHandle, seconds:u64,expected_span:f64)->(usize,usize,f64,f64) {
 let mut iq=vec![0f32;16384];let mut values=0usize;let mut frames=0usize;let mut power=0f64;let mut cr=0f64;let mut ci=0f64;
 let until=Instant::now()+Duration::from_secs(seconds);
 while Instant::now()<until {
  let n=h.rx_read(&mut iq);values+=n;
  assert!(iq[..n].iter().all(|v|v.is_finite()));
  power+=iq[..n].iter().map(|v|f64::from(*v).powi(2)).sum::<f64>();
  let mut previous:Option<(f64,f64)>=None;
  for pair in iq[..n].chunks_exact(2) {let (r,i)=(f64::from(pair[0]),f64::from(pair[1]));if let Some((pr,pi))=previous {cr+=r*pr+i*pi;ci+=i*pr-r*pi;}previous=Some((r,i));}
  if let Some(f)=h.take_fft() {
   assert_eq!(f.bins.len(),2048);assert!((f.span_hz-expected_span).abs()<1.0);
   assert!(f.bins.iter().all(|v|v.is_finite()));frames+=1;
  }
  assert!(h.is_alive(),"SpyServer client disconnected");thread::sleep(Duration::from_millis(5));
 }
 (values,frames,power/values.max(1) as f64,ci.atan2(cr)*h.sample_rate_hz/std::f64::consts::TAU)
}
fn main() {
 tracing_subscriber::fmt().with_env_filter("info").init();
 let address=std::env::var("AIRSPY_TEST_ADDRESS").expect("set AIRSPY_TEST_ADDRESS to HOST:5555");
 let stage:u32=std::env::var("AIRSPY_TEST_FFT_STAGE").ok().map(|v|v.parse().unwrap()).unwrap_or(0);
 let iq_stage:i32=std::env::var("AIRSPY_TEST_IQ_STAGE").ok().map(|v|v.parse().unwrap()).unwrap_or(3);
 let offset:f64=std::env::var("AIRSPY_TEST_OFFSET").ok().map(|v|v.parse().unwrap()).unwrap_or(50000.0);
 let cfg=SpyServerConfig{address,iq_decimation:iq_stage,fft_enabled:true,fft_decimation:stage,gain_index:0,..Default::default()};
 let mut h=SpyServerHandle::connect_wideband(&cfg,7074000.0).expect("connect");
 assert_eq!(h.info.maximum_bandwidth,660000);assert_eq!(h.sample_rate_hz,768000.0/f64::from(1u32<<iq_stage));
 let minimum=(h.sample_rate_hz*2.0*15.0*0.85) as usize;
 let (v0,f0,_,_)=receive(&mut h,15,660000.0/f64::from(1u32<<stage));assert!(v0>minimum && f0>50,"IQ values={v0}, FFT frames={f0}");
 let original=h.device_center_hz();let requested=7074000.0+offset;h.set_center_hz(requested);
 let deadline=Instant::now()+Duration::from_secs(5);
 while (h.iq_center_hz()-requested).abs()>1.0 {assert!(Instant::now()<deadline,"IQ retune timed out");thread::sleep(Duration::from_millis(20));}
 let (v1,f1,power,freq)=receive(&mut h,15,660000.0/f64::from(1u32<<stage));assert!(v1>minimum && f1>50,"IQ values={v1}, FFT frames={f1}");
 if std::env::var_os("AIRSPY_FAKE_TONE").is_some(){assert!(power>0.0001 && (freq-1000.0).abs()<20.0,"shifted tone: power={power}, frequency={freq}");}
 assert!((h.device_center_hz()-original).abs()<1.0,"device retuned despite IQ offset fitting within FFT window");
 println!("SDROxide 1.7.2 FFT+IQ: samples={v0}/{v1}, FFT frames={f0}/{f1}, device center={original}, IQ center={}",h.iq_center_hz());
 h.release();
}
