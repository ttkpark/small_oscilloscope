#!/usr/bin/env python3
"""
Small Oscilloscope - Analog Test Script
아날로그 회로 테스트를 위한 Python 스크립트
"""

import serial
import time
import json
import requests
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from collections import deque
import argparse

class AnalogTester:
    def __init__(self, serial_port='COM3', baudrate=115200, web_url='http://192.168.4.1'):
        self.serial_port = serial_port
        self.baudrate = baudrate
        self.web_url = web_url
        self.ser = None
        self.data_points = deque(maxlen=100)
        
    def connect_serial(self):
        """시리얼 포트 연결"""
        try:
            self.ser = serial.Serial(self.serial_port, self.baudrate, timeout=1)
            print(f"시리얼 포트 {self.serial_port}에 연결되었습니다.")
            return True
        except Exception as e:
            print(f"시리얼 포트 연결 실패: {e}")
            return False
    
    def disconnect_serial(self):
        """시리얼 포트 연결 해제"""
        if self.ser:
            self.ser.close()
            print("시리얼 포트 연결이 해제되었습니다.")
    
    def read_serial_data(self):
        """시리얼 데이터 읽기"""
        if not self.ser:
            return None
        
        try:
            if self.ser.in_waiting:
                line = self.ser.readline().decode('utf-8').strip()
                return line
        except Exception as e:
            print(f"시리얼 데이터 읽기 오류: {e}")
        
        return None
    
    def get_web_data(self):
        """웹 서버에서 데이터 가져오기"""
        try:
            response = requests.get(f"{self.web_url}/data", timeout=1)
            if response.status_code == 200:
                return response.json()
        except Exception as e:
            print(f"웹 데이터 가져오기 오류: {e}")
        
        return None
    
    def parse_serial_line(self, line):
        """시리얼 라인 파싱"""
        if not line or not line.startswith("ADC:"):
            return None
        
        try:
            # 예상 형식: "ADC: CH0=1234mV, CH1=5678mV | CH0: DC, 1X/10X, 1500mV | CH1: AC, 10X/100X, 2000mV"
            parts = line.split('|')
            if len(parts) < 3:
                return None
            
            # ADC 값 추출
            adc_part = parts[0]
            adc0_match = adc_part.split('CH0=')[1].split('mV')[0]
            adc1_match = adc_part.split('CH1=')[1].split('mV')[0]
            
            adc0 = int(adc0_match)
            adc1 = int(adc1_match)
            
            # 채널 설정 추출
            ch0_config = parts[1].strip()
            ch1_config = parts[2].strip()
            
            return {
                'timestamp': time.time(),
                'adc0': adc0,
                'adc1': adc1,
                'ch0_config': ch0_config,
                'ch1_config': ch1_config
            }
        except Exception as e:
            print(f"시리얼 라인 파싱 오류: {e}")
            return None
    
    def monitor_serial(self, duration=60):
        """시리얼 모니터링"""
        print(f"시리얼 모니터링을 시작합니다. (지속시간: {duration}초)")
        print("Ctrl+C로 중단할 수 있습니다.")
        
        start_time = time.time()
        data_count = 0
        
        try:
            while time.time() - start_time < duration:
                line = self.read_serial_data()
                if line:
                    data = self.parse_serial_line(line)
                    if data:
                        data_count += 1
                        print(f"[{data_count:04d}] {line}")
                        
                        # 데이터 포인트 저장
                        self.data_points.append(data)
                
                time.sleep(0.1)
                
        except KeyboardInterrupt:
            print("\n모니터링이 중단되었습니다.")
        
        print(f"총 {data_count}개의 데이터 포인트를 수집했습니다.")
    
    def plot_data(self):
        """수집된 데이터 플롯"""
        if not self.data_points:
            print("플롯할 데이터가 없습니다.")
            return
        
        # 데이터 분리
        timestamps = [d['timestamp'] for d in self.data_points]
        adc0_values = [d['adc0'] for d in self.data_points]
        adc1_values = [d['adc1'] for d in self.data_points]
        
        # 시간을 상대 시간으로 변환
        start_time = timestamps[0]
        relative_times = [t - start_time for t in timestamps]
        
        # 플롯 생성
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 8))
        
        # ADC 값 플롯
        ax1.plot(relative_times, adc0_values, 'y-', label='CH0', linewidth=2)
        ax1.plot(relative_times, adc1_values, 'c-', label='CH1', linewidth=2)
        ax1.set_xlabel('시간 (초)')
        ax1.set_ylabel('전압 (mV)')
        ax1.set_title('ADC 읽기 값')
        ax1.legend()
        ax1.grid(True)
        
        # 설정 정보 표시
        if self.data_points:
            latest = self.data_points[-1]
            config_text = f"CH0: {latest['ch0_config']}\nCH1: {latest['ch1_config']}"
            ax2.text(0.1, 0.5, config_text, transform=ax2.transAxes, 
                    fontsize=12, verticalalignment='center',
                    bbox=dict(boxstyle="round,pad=0.3", facecolor="lightgray"))
            ax2.set_xlim(0, 1)
            ax2.set_ylim(0, 1)
            ax2.axis('off')
            ax2.set_title('현재 채널 설정')
        
        plt.tight_layout()
        plt.show()
    
    def test_ch423(self):
        """CH423 테스트"""
        print("CH423 테스트를 시작합니다...")
        
        if not self.ser:
            print("시리얼 포트가 연결되지 않았습니다.")
            return
        
        # CH423 테스트 명령 전송
        test_commands = [
            "CH423_TEST",
            "CH423_READ",
            "CH423_PATTERN"
        ]
        
        for cmd in test_commands:
            print(f"명령 전송: {cmd}")
            self.ser.write(f"{cmd}\n".encode())
            time.sleep(1)
            
            # 응답 읽기
            response = self.ser.readline().decode('utf-8').strip()
            if response:
                print(f"응답: {response}")
    
    def set_channel_config(self, channel, mode, primary, secondary, voltage):
        """채널 설정"""
        if not self.ser:
            print("시리얼 포트가 연결되지 않았습니다.")
            return
        
        config_cmd = f"SET_CH{channel} {mode} {primary} {secondary} {voltage}\n"
        print(f"채널 {channel} 설정: {config_cmd.strip()}")
        self.ser.write(config_cmd.encode())
        
        # 응답 대기
        time.sleep(0.5)
        response = self.ser.readline().decode('utf-8').strip()
        if response:
            print(f"응답: {response}")

def main():
    parser = argparse.ArgumentParser(description='Small Oscilloscope Analog Test Tool')
    parser.add_argument('--port', default='COM3', help='시리얼 포트 (기본값: COM3)')
    parser.add_argument('--baudrate', type=int, default=115200, help='보드레이트 (기본값: 115200)')
    parser.add_argument('--web-url', default='http://192.168.4.1', help='웹 서버 URL')
    parser.add_argument('--duration', type=int, default=60, help='모니터링 지속시간 (초)')
    parser.add_argument('--plot', action='store_true', help='데이터 플롯 표시')
    parser.add_argument('--test-ch423', action='store_true', help='CH423 테스트 실행')
    parser.add_argument('--set-ch0', nargs=4, metavar=('MODE', 'PRIMARY', 'SECONDARY', 'VOLTAGE'), 
                       help='채널 0 설정 (예: DC 1 10 1500)')
    parser.add_argument('--set-ch1', nargs=4, metavar=('MODE', 'PRIMARY', 'SECONDARY', 'VOLTAGE'), 
                       help='채널 1 설정 (예: AC 10 100 2000)')
    
    args = parser.parse_args()
    
    tester = AnalogTester(args.port, args.baudrate, args.web_url)
    
    if not tester.connect_serial():
        return
    
    try:
        if args.test_ch423:
            tester.test_ch423()
        
        if args.set_ch0:
            mode, primary, secondary, voltage = args.set_ch0
            tester.set_channel_config(0, mode, int(primary), int(secondary), int(voltage))
        
        if args.set_ch1:
            mode, primary, secondary, voltage = args.set_ch1
            tester.set_channel_config(1, mode, int(primary), int(secondary), int(voltage))
        
        if args.plot:
            tester.monitor_serial(args.duration)
            tester.plot_data()
        else:
            tester.monitor_serial(args.duration)
    
    finally:
        tester.disconnect_serial()

if __name__ == "__main__":
    main() 