// app/(tabs)/settings.tsx
import React, { useState, useEffect } from 'react';
import { StyleSheet, View, Text, TextInput, Button } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';
import AsyncStorage from '@react-native-async-storage/async-storage';

const Settings = () => {
    const [doorbellIp, setDoorbellIp] = useState('');
    const [streamUrl, setStreamUrl] = useState(
        'http://192.168.178.66:10001/stream.mjpg'
    );
    const [timeout, setTimeout] = useState('');

    useEffect(() => {
        const loadSettings = async () => {
            const savedDoorbellIp = await AsyncStorage.getItem('doorbellIp');
            const savedStreamUrl = await AsyncStorage.getItem('streamUrl');
            const savedTimeout = await AsyncStorage.getItem('timeout');
            if (savedStreamUrl) setStreamUrl(savedStreamUrl);
            if (savedTimeout) setTimeout(savedTimeout);
        };

        loadSettings();
    }, []);

    const handleSave = async () => {
        await AsyncStorage.setItem('doorbellIp', doorbellIp);
        await AsyncStorage.setItem('streamUrl', streamUrl);
        await AsyncStorage.setItem('timeout', timeout);
        console.log('Settings saved.');
    };

    return (
        <SafeAreaView style={styles.container}>
            <Text style={styles.label}>Doorbell IP</Text>
            <TextInput
                style={styles.input}
                value={doorbellIp}
                onChangeText={setDoorbellIp}
                placeholder="Enter Doorbell IP"
            />
            <Text style={styles.label}>Stream URL</Text>
            <TextInput
                style={styles.input}
                value={streamUrl}
                onChangeText={setStreamUrl}
                placeholder="Enter Stream URL"
            />
            <Text style={styles.label}>Timeout (ms)</Text>
            <TextInput
                style={styles.input}
                value={timeout}
                onChangeText={setTimeout}
                keyboardType="numeric"
                placeholder="Enter Timeout"
            />
            <Button title="Save Settings" onPress={handleSave} />
        </SafeAreaView>
    );
};

const styles = StyleSheet.create({
    container: {
        flex: 1,
        padding: 20,
    },
    label: {
        fontSize: 16,
        marginBottom: 8,
    },
    input: {
        height: 40,
        borderColor: 'gray',
        borderWidth: 1,
        marginBottom: 20,
        paddingHorizontal: 10,
        color: 'black',
    },
    picker: {
        height: 50,
        width: '100%',
        marginBottom: 20,
    },
});

export default Settings;
