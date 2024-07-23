// components/Livestream.tsx
import React, { useState, useEffect } from 'react';
import {
    StyleSheet,
    View,
    Text,
    Button,
    ActivityIndicator,
} from 'react-native';
import { WebView } from 'react-native-webview';
import AsyncStorage from '@react-native-async-storage/async-storage';

const Livestream = ({ reload }) => {
    const [url, setUrl] = useState<string | null>(null);
    const [error, setError] = useState<string | null>(null);
    const [loading, setLoading] = useState(true);

    useEffect(() => {
        const loadUrl = async () => {
            try {
                const savedUrl = await AsyncStorage.getItem('streamUrl');
                if (savedUrl) {
                    console.log('Loaded URL from AsyncStorage:', savedUrl);
                    setUrl(savedUrl);
                    setLoading(false);
                } else {
                    console.error('No URL found in AsyncStorage.');
                    setError(
                        'No URL found. Please set the stream URL in the settings.'
                    );
                    setLoading(false);
                }
            } catch (e) {
                console.error('Error loading URL from AsyncStorage:', e);
                setError('Error loading URL from storage.');
                setLoading(false);
            }
        };

        loadUrl();
    }, [reload]);

    const handleLoad = () => {
        setLoading(false);
        setError(null);
    };

    const handleError = () => {
        setError('Error loading stream. Please try reloading.');
        setLoading(false);
    };

    return (
        <View style={styles.container}>
            {loading && <ActivityIndicator size="large" color="#0000ff" />}
            {error && <Text style={styles.errorText}>{error}</Text>}
            {url && !error && (
                <WebView
                    source={{ uri: url }}
                    style={styles.webview}
                    onLoad={handleLoad}
                    onError={handleError}
                />
            )}
            {error && <Button title="Reload Stream" onPress={reload} />}
        </View>
    );
};

const styles = StyleSheet.create({
    container: {
        flex: 1,
        justifyContent: 'center',
        alignItems: 'center',
    },
    webview: {
        width: '100%',
        height: '100%',
    },
    errorText: {
        color: 'red',
        textAlign: 'center',
        marginBottom: 10,
    },
});

export default Livestream;
