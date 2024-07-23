import React, { useState } from 'react';
import { StyleSheet, View, Button } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';
import Livestream from '../../components/Livestream';

const Index = () => {
    const [reloadKey, setReloadKey] = useState(0);

    const reloadStream = () => {
        setReloadKey((prevKey) => prevKey + 1);
    };

    return (
        <SafeAreaView style={styles.container}>
            <View style={styles.streamContainer}>
                <Livestream reload={reloadStream} key={reloadKey} />
            </View>
            <View style={styles.buttonContainer}>
                <Button title="Button 1" onPress={() => {}} />
                <Button title="Button 2" onPress={() => {}} />
            </View>
        </SafeAreaView>
    );
};

const styles = StyleSheet.create({
    container: {
        flex: 1,
    },
    streamContainer: {
        flex: 1,
    },
    buttonContainer: {
        height: 80,
        justifyContent: 'space-around',
        alignItems: 'center',
        flexDirection: 'row',
        padding: 10,
    },
});

export default Index;
