use std::{io::{Read,Write},net::TcpStream,time::{Duration,Instant},thread};
use sdroxide_spyserver::SpyServerHandle;
use sdroxide_types::SpyServerConfig;
fn status()->String {
 let host=std::env::var("AIRSPY_HOST").expect("set AIRSPY_HOST");
 let mut s=TcpStream::connect((host.as_str(),80)).unwrap();s.set_read_timeout(Some(Duration::from_secs(3))).unwrap();
 s.write_all(format!("GET /api/status HTTP/1.0\r\nHost: {host}\r\n\r\n").as_bytes()).unwrap();let mut out=Vec::new();let mut chunk=[0u8;2048];loop { let n=s.read(&mut chunk).unwrap();if n==0 { break } out.extend_from_slice(&chunk[..n]);if out.windows(4).position(|w|w==b"\r\n\r\n").is_some() && out.last()==Some(&b'}') {break} } String::from_utf8(out).unwrap()
}
fn wait_status(fragment:&str){let until=Instant::now()+Duration::from_secs(4);loop{if status().contains(fragment){return}assert!(Instant::now()<until,"status did not contain {fragment}");thread::sleep(Duration::from_millis(100));}}
fn main(){
 tracing_subscriber::fmt().with_env_filter("info").init();
 let cfg=SpyServerConfig{address:format!("{}:5555",std::env::var("AIRSPY_HOST").expect("set AIRSPY_HOST")),iq_decimation:3,gain_index:0,..Default::default()};
 let mut h=SpyServerHandle::connect_wideband(&cfg,7074000.0).expect("connect");
 assert_eq!(h.sample_rate_hz,96000.0);assert_eq!(h.iq_decimation,3);assert_eq!(h.info.maximum_gain_index,9);assert!(h.can_control());
 wait_status("\"iq_sample_rate\":96000,");
 let mut buf=vec![0f32;16384];let mut values=0usize;let mut nonzero=0usize;let start=Instant::now();
 while start.elapsed()<Duration::from_secs(12){
  let n=h.rx_read(&mut buf);values+=n;nonzero+=buf[..n].iter().filter(|&&v|v!=0.0&&v.is_finite()).count();
  thread::sleep(Duration::from_millis(1));
 }
 assert!(h.is_alive()&&values>1500000&&nonzero>0,"values={values} nonzero={nonzero}");
 h.set_gain_index(1);wait_status("\"receiver_gain\":1,");
 h.set_gain_index(9);wait_status("\"receiver_gain\":9,");
 h.set_gain_index(0);wait_status("\"receiver_gain\":0,");
 h.set_center_hz(7074375.0);wait_status("\"frequency\":7074375,");
 let until=Instant::now()+Duration::from_secs(4);while (h.iq_center_hz()-7074375.0).abs()>1.0{assert!(Instant::now()<until,"client did not receive center sync");thread::sleep(Duration::from_millis(50));}
 println!("SDROxide 1.7.2: stage {}, {} Hz, {} decoded I/Q values, {} nonzero; gain 1/9/0 applied; retune synchronized; FFT span {}",h.iq_decimation,h.sample_rate_hz,values,nonzero,h.fft_span_hz);
 h.release();
}
